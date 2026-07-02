#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>   // 中継サーバへの HTTP POST（ESP32 標準）
#include <ArduinoJson.h>  // リクエスト body の安全な組み立て
#include <string>
#include <vector>
#include <math.h>     // cosf / sinf（流れ場に沿った曲線の積分に使う）
#include "avatar.h"
#include "sheep.h"
#include "art.h"      // art_generate（幾何学アートの図形プリミティブ生成・純粋ロジック）
#include "gesture.h"  // touch_update（短タップ/長押し検出・純粋ロジック）
#include "scene.h"    // next_scene（シーン巡回・純粋ロジック）
#include "voice.h"    // voice_baa_pcm（メェの PCM 合成・純粋ロジック）
#include "gem.h"      // Gem / gem_count / gem_at（宝石図鑑データ・純粋ロジック）
#include "gem3d.h"    // gem3d_*（宝石3D回転の純粋数学：回転/投影/カリング/法線/明るさ）
#include "wav.h"      // parse_wav_header（/tts の WAV から PCM を取り出す・純粋ロジック）
#include "net.h"
#include "pokemon.h"  // Pokemon / parse_pokemon_info（/pokemon/info の JSON→構造体・純粋ロジック）
#include "secrets.h"  // WIFI_SSID / WIFI_PASS / RELAY_URL（git管理外。secrets.h.example を参照）

// 画面レイアウト定数（320x240 を setRotation(1) で使う想定）。
constexpr int kScreenW = 320;
constexpr int kScreenH = 240;

// 顔（中心・半径）。
constexpr int kFaceCx = kScreenW / 2;
constexpr int kFaceCy = kScreenH / 2;
constexpr int kFaceR  = 90;

// 目（左右の中心X、共通の中心Y、最大の半幅・半高）。
constexpr int kEyeY        = kFaceCy - 20;
constexpr int kEyeLx       = kFaceCx - 40;
constexpr int kEyeRx       = kFaceCx + 40;
constexpr int kEyeHalfW    = 16;  // 横はまばたきで変えない
constexpr int kEyeHalfHMax = 22;  // 開ききった時の半分の高さ（Normal）
// 目領域のクリア範囲（表情で目の大きさが変わっても前フレームを確実に消すため、最大値で固定）。
constexpr int kEyeClearHalfW = 20;
constexpr int kEyeClearHalfH = 26;

// 眉（目の上に描く線）。表情で形が変わる。アニメしないので表情変化時だけ描く。
constexpr int kBrowBaseY = kEyeY - 40;  // 眉の基準Y
constexpr int kBrowHalfW = 18;          // 眉の半幅
constexpr int kBrowRaise = 7;           // 「上げ眉」の持ち上げ量
constexpr int kBrowSlant = 4;           // 「ハの字」等の傾き量
constexpr int kBrowThick = 3;           // 眉の太さ(px)

// 口（中心・最大の半幅・半高）。口パクで高さが変わる。
constexpr int kMouthCx       = kFaceCx;
constexpr int kMouthCy       = kFaceCy + 40;
constexpr int kMouthHalfW    = 30;
constexpr int kMouthHalfHMax = 14;  // 開ききった時の半分の高さ

// 口パクのデモ用スケジュール（実トリガーは後続の対話レイヤーで差し替え予定）。
constexpr uint32_t kSpeakPeriodMs = 4000;  // 4秒周期で
constexpr uint32_t kSpeakOnMs     = 2000;  // 先頭2秒だけ喋る

// Wi-Fi 接続を諦めるまでの時間（これを超えたら Failed 表示にする）。
constexpr uint32_t kWifiTimeoutMs = 15000;
uint32_t g_wifiBeginMs = 0;  // WiFi.begin を呼んだ時刻

// 対話状態：Wi-Fi 接続後に一度だけ中継サーバへ問い合わせる（最小トリガ。実トリガは②-3）。
bool g_hasReply = false;
std::string g_reply;                              // 画面下部に出す返答文
Expression g_requestedExpr = Expression::Neutral;  // 中継サーバが要求した表情
uint32_t g_exprRequestMs = 0;                      // 表情を要求された時刻（自動復帰の起点）
// 再問い合わせ直後の「強制再描画」フラグ。同じ表情が返った時でも返答文を必ず更新するために使う
// （描画は通常「表情が変わった時だけ」最適化されているので、それを1フレームだけ上書きする）。
bool g_replyDirty = false;

// 色（M5GFX の RGB565）。
constexpr uint16_t kColBg    = TFT_BLACK;
constexpr uint16_t kColFace  = 0xFE19;  // 肌色っぽいピンク
constexpr uint16_t kColEye   = TFT_BLACK;
constexpr uint16_t kColMouth = 0x8000;  // 暗い赤

// 目スタイルから「最大半高・半幅」を決める（描画の寸法は main の責務）。
//   Normal … 既定 / Wide … 見開き(大きく) / Squint … 細め(低く)
static void eyeMetrics(EyeStyle style, int& maxHalfH, int& maxHalfW) {
    switch (style) {
        case EyeStyle::Wide:   maxHalfH = 25; maxHalfW = 18; break;
        case EyeStyle::Squint: maxHalfH = 10; maxHalfW = 16; break;
        case EyeStyle::Normal:
        default:               maxHalfH = kEyeHalfHMax; maxHalfW = kEyeHalfW; break;
    }
}

// 目を1つ描く。openness(0.0..1.0) と目スタイルの最大寸法から高さを決める。
// クリアは固定の最大領域(kEyeClearHalf*)で行い、表情が変わっても前フレームを消し残さない。
static void drawEye(int cx, float openness, int maxHalfH, int maxHalfW) {
    M5.Display.fillRect(cx - kEyeClearHalfW, kEyeY - kEyeClearHalfH,
                        kEyeClearHalfW * 2, kEyeClearHalfH * 2, kColFace);

    const int halfH = static_cast<int>(maxHalfH * openness);
    if (halfH <= 1) {
        // ほぼ閉じている → 横一本の線で「とじ目」を表現
        M5.Display.fillRect(cx - maxHalfW, kEyeY - 1, maxHalfW * 2, 2, kColEye);
    } else {
        M5.Display.fillRect(cx - maxHalfW, kEyeY - halfH, maxHalfW * 2, halfH * 2, kColEye);
    }
}

// 太い線分を描く（眉・口角に使う。drawLine を縦にずらして太さを出す）。
static void drawThickLine(int x0, int y0, int x1, int y1, int thick, uint16_t col) {
    for (int dy = 0; dy < thick; ++dy) {
        M5.Display.drawLine(x0, y0 + dy, x1, y1 + dy, col);
    }
}

// 閉じている口の「形」を表情ごとに描く。
//   Line  … 横一文字 / Smile … 口角を上げた笑顔(‿) / Frown … 口角を下げた(⌒) / Round … 丸い口(o)
static void drawRestingMouth(MouthShape shape) {
    const int xl = kMouthCx - kMouthHalfW;
    const int xr = kMouthCx + kMouthHalfW;
    const int y  = kMouthCy;
    switch (shape) {
        case MouthShape::Smile:
            // 中央を下げ両端を上げる V字2本（下に凸＝笑顔）
            drawThickLine(xl, y - 6, kMouthCx, y + 4, kBrowThick, kColMouth);
            drawThickLine(kMouthCx, y + 4, xr, y - 6, kBrowThick, kColMouth);
            break;
        case MouthShape::Frown:
            // 中央を上げ両端を下げる（上に凸＝への字）
            drawThickLine(xl, y + 4, kMouthCx, y - 6, kBrowThick, kColMouth);
            drawThickLine(kMouthCx, y - 6, xr, y + 4, kBrowThick, kColMouth);
            break;
        case MouthShape::Round:
            // 驚きの丸い口
            M5.Display.fillCircle(kMouthCx, y, 9, kColMouth);
            break;
        case MouthShape::Line:
        default:
            M5.Display.fillRect(xl, y - 1, kMouthHalfW * 2, 2, kColMouth);
            break;
    }
}

// 口を描く。喋っている間(openness>0)は楕円で開閉、閉じている時は表情ごとの形を描く。
// まず口の最大領域を顔色でクリアしてから描く（ちらつき抑制）。
static void drawMouth(float openness, MouthShape shape) {
    M5.Display.fillRect(kMouthCx - kMouthHalfW, kMouthCy - kMouthHalfHMax,
                        kMouthHalfW * 2, kMouthHalfHMax * 2, kColFace);

    const int halfH = static_cast<int>(kMouthHalfHMax * openness);
    if (halfH <= 1) {
        // ほぼ閉じている → 表情ごとの口の形
        drawRestingMouth(shape);
    } else {
        // 開いている（喋り中） → 楕円で口の中を表現
        M5.Display.fillEllipse(kMouthCx, kMouthCy, kMouthHalfW, halfH, kColMouth);
    }
}

// 変化しない部分（顔の輪郭）を一度だけ描く。
static void drawStaticFace() {
    M5.Display.fillScreen(kColBg);
    M5.Display.fillCircle(kFaceCx, kFaceCy, kFaceR, kColFace);
}

// 片方の眉を描く。isLeft で左右を区別し、ハの字/片眉上げの向きを決める。
static void drawBrow(int cx, BrowShape shape, bool isLeft) {
    const int xl = cx - kBrowHalfW;
    const int xr = cx + kBrowHalfW;
    int yl = kBrowBaseY;  // 外側(左端)のY
    int yr = kBrowBaseY;  // 内側寄り(右端)のY
    switch (shape) {
        case BrowShape::Raised:
            yl -= kBrowRaise; yr -= kBrowRaise;
            break;
        case BrowShape::Worried:
            // ハの字：内側を上げる。内側は左目なら右端、右目なら左端。
            if (isLeft) { yl += kBrowSlant; yr -= kBrowSlant; }
            else        { yl -= kBrowSlant; yr += kBrowSlant; }
            break;
        case BrowShape::Quizzical:
            // 片眉だけ上げる（右目側を上げて「？」感を出す）。
            if (!isLeft) { yl -= kBrowRaise; yr -= kBrowRaise; }
            break;
        case BrowShape::Flat:
        default:
            break;  // 水平のまま
    }
    drawThickLine(xl, yl, xr, yr, kBrowThick, kColEye);
}

// 両眉を描く。アニメしないので表情変化時にだけ呼ぶ。
// 先に眉領域を顔色でクリアしてから描き直す（前の形を消す）。
static void drawBrows(BrowShape shape) {
    const int y0 = kBrowBaseY - kBrowRaise - 2;
    const int h  = kBrowRaise + kBrowSlant + kBrowThick + 6;
    M5.Display.fillRect(kEyeLx - kBrowHalfW - 2, y0,
                        (kEyeRx - kEyeLx) + 2 * kBrowHalfW + 4, h, kColFace);
    drawBrow(kEyeLx, shape, true);
    drawBrow(kEyeRx, shape, false);
}

// 画面上部に Wi-Fi 接続状態の文言を描く。状態が変わった時だけ呼ぶ。
static void drawWifiStatus(WifiState state) {
    // 上部の帯（顔は y=30 付近から始まるので 0..18 は被らない）をクリアして描き直す
    M5.Display.fillRect(0, 0, kScreenW, 18, kColBg);
    M5.Display.setFont(&fonts::Font0);  // ASCII 既定フォント（対話描画で和文に切替えるため明示）
    M5.Display.setTextColor(TFT_WHITE, kColBg);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(4, 2);
    M5.Display.print(wifi_status_text(state).c_str());
}

// Expression を画面表示用の短いラベルに変換する（表情の見える化・暫定。本格描画は① スプライト）。
static const char* expressionLabel(Expression e) {
    switch (e) {
        case Expression::Happy:     return "happy";
        case Expression::Thinking:  return "thinking";
        case Expression::Sad:       return "sad";
        case Expression::Surprised: return "surprised";
        case Expression::Neutral:   return "neutral";
    }
    return "neutral";
}

// 画面下部に「現在の表情ラベル＋返答文」を描く。
// 和文は既定フォントで出ないので lgfxJapanGothic_16 に切り替える。
static void drawDialog(const std::string& reply, Expression expr) {
    M5.Display.fillRect(0, 190, kScreenW, kScreenH - 190, kColBg);

    // 表情ラベル（ASCII・小）
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_CYAN, kColBg);
    M5.Display.setCursor(4, 192);
    M5.Display.printf("[%s]", expressionLabel(expr));

    // 返答文（和文フォント。setTextWrap 既定 true で画面端折り返し）
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextColor(TFT_WHITE, kColBg);
    M5.Display.setCursor(4, 206);
    M5.Display.print(reply.c_str());

    M5.Display.setFont(&fonts::Font0);  // 既定に戻す（他描画への影響回避）
}

// 中継 /chat(Claude) に message を投げ、返答を out に格納する（実機依存部）。成否を返す。
// 起動挨拶(fetchGreeting)と対話ループ(sheepOnTap)の両方から使う共通の問い合わせ部。
// ※ Claude の推論は数秒かかるため、/stt と同様に既定5秒のタイムアウトを延ばす（既定だと -11）。
static bool fetchChatReply(const std::string& message, ReplyMessage& out) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    http.begin(RELAY_URL);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(20000);  // Claude 推論待ち（読み取りタイムアウト回避）

    // リクエスト body を ArduinoJson で組み立て（特殊文字も安全にエスケープ）。
    JsonDocument req;
    req["message"] = message.c_str();
    std::string body;
    serializeJson(req, body);

    const int code = http.POST(String(body.c_str()));
    if (code != 200) { http.end(); return false; }
    out = parse_relay_reply(http.getString().c_str());
    http.end();
    return true;
}

// 起動挨拶として /chat に一度問い合わせ、結果を g_reply / g_requestedExpr に格納する（実機依存部）。
static void fetchGreeting() {
    ReplyMessage m;
    if (fetchChatReply("起動したよ。ひとこと挨拶して。", m)) {
        g_reply = m.reply;
        g_requestedExpr = m.expression;
    } else {
        // 失敗時も画面で分かるように（sad 表情）。
        g_reply = "relay error";
        g_requestedExpr = Expression::Sad;
    }
    g_exprRequestMs = millis();
    g_hasReply = true;
    g_replyDirty = true;  // 新しい返答が来たので、次フレームでダイアログと表情を必ず描き直す
}

// 実際の WiFi 接続状態を、表示用の WifiState に変換する（実機依存部）。
static WifiState currentWifiState(uint32_t now) {
    if (WiFi.status() == WL_CONNECTED) {
        return WifiState::Connected;
    }
    if (now - g_wifiBeginMs > kWifiTimeoutMs) {
        return WifiState::Failed;
    }
    return WifiState::Connecting;
}

// ───────── 羊シーン（Issue #28 / epic #27） ─────────
// 羊の配置（rotation(1) の 320x240・中央）。bob で全体が上下する。
constexpr int kSheepCx = kScreenW / 2;
constexpr int kSheepCy = kScreenH / 2 + 4;
// 毎フレーム全体を描き直すための固定クリア枠（bob の振れ幅・足まで含む）。
// 高さは羊の最下点（bob最大時で canvas-local ≈140px：胴毛の瘤）に小マージンを足した値に
// 抑える。枠の底を画面 y=44+146=190 手前で止め、返答文領域(drawDialog: y=190〜)と分離する
// （枠を毎フレーム背景色で塗り直すため、ここが文字に被ると文字が消える）。Issue #62。
constexpr int kSheepClipX = 56;
constexpr int kSheepClipY = 44;
constexpr int kSheepClipW = 208;
constexpr int kSheepClipH = 146;

// 羊の色（RGB565）。
constexpr uint16_t kColWool      = TFT_WHITE;
constexpr uint16_t kColSheepFace = 0xF7BB;  // クリーム色の顔
constexpr uint16_t kColSheepLeg  = 0x4208;  // 暗いグレーの足
constexpr uint16_t kColEar       = 0x8410;  // グレーの耳
constexpr uint16_t kColSheepEye  = TFT_BLACK;

// ちらつき防止のオフスクリーン・キャンバス（ダブルバッファ／Issue #41）。
// 画面に直接「消去→描画」すると消えた瞬間が見えてちらつくため、まずこのメモリ上の
// キャンバスへ全部描いてから pushSprite で一括転送する（途中状態を画面に出さない）。
static M5Canvas g_sheepCanvas(&M5.Display);

// 羊の目を1つ描く（キャンバスへ）。まばたきは既存のテスト済み eye_openness を流用する。
static void drawSheepEye(M5Canvas& cv, int ex, int ey, float openness) {
    const int halfH = static_cast<int>(7 * openness);
    if (halfH <= 1) {
        cv.fillRect(ex - 4, ey - 1, 8, 2, kColSheepEye);  // ほぼ閉じ＝とじ目
    } else {
        cv.fillEllipse(ex, ey, 4, halfH, kColSheepEye);
    }
}

// メェの合成 PCM（自前生成）。setup で一度だけ作り、playRaw で鳴らす（P2 M1 / Issue #44）。
static int16_t g_baaPcm[kBaaSamples];
static int     g_baaLen = 0;

// 「メェ」を鳴らす。tone() の単音ではなく、合成した波形(PCM)を playRaw で再生する
// （クラウド TTS でもサーバが返す PCM をこの経路で鳴らす＝同じ playRaw を先に通す）。
static void playBleat() {
    M5.Speaker.setVolume(180);
    M5.Speaker.playRaw(g_baaPcm, g_baaLen, kVoiceSampleRate, false);  // false = モノラル
}

// 中継サーバの /tts エンドポイント URL を RELAY_URL（…/chat）から導出する。
// secrets に専用定義を増やさず、末尾 /chat を /tts に置換するだけ（無ければそのまま）。
static std::string ttsUrlFromRelay() {
    std::string u = RELAY_URL;
    const std::string chat = "/chat";
    if (u.size() >= chat.size() &&
        u.compare(u.size() - chat.size(), chat.size(), chat) == 0) {
        u.replace(u.size() - chat.size(), chat.size(), "/tts");
    }
    return u;
}

// 受信済み WAV バッファから PCM 本体を取り出して playRaw で鳴らす共通末尾。
// TTS(/tts) と鳴き声(/pokemon/cry) は「取得の仕方」だけ違い、ヘッダ剥がし→再生は同一なので
// ここに集約する（speakTts / speakCry が共有・DRY）。parse_wav_header は native テスト済みの純粋層。
static bool playWavBuffer(const uint8_t* buf, size_t len) {
    WavInfo info;
    if (!parse_wav_header(buf, len, &info)) return false;

    const int16_t* pcm = reinterpret_cast<const int16_t*>(buf + info.data_offset);
    const size_t samples = info.data_bytes / 2;  // 16bit = 2byte / サンプル
    M5.Speaker.setVolume(180);
    return M5.Speaker.playRaw(pcm, samples, info.sample_rate, info.channels == 2);
}

// 受信した WAV を置くバッファ。playRaw 再生中も生かしておく必要があるため static で保持し、
// 次回呼び出しの先頭で解放する（再生中に free しない）。容量が読めないので PSRAM を使う。
static uint8_t* g_ttsBuf = nullptr;

// 中継 /tts に text を投げ、返ってきた WAV を playRaw で鳴らす（実機依存部・P2 M2b / Issue #48）。
// 成否を返し、失敗時は呼び出し側で自前メェにフォールバックさせる（オフラインでも必ず鳴る）。
static bool speakTts(const std::string& text) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    http.begin(ttsUrlFromRelay().c_str());
    http.addHeader("Content-Type", "application/json");

    // body は ArduinoJson で組み立て（特殊文字を安全にエスケープ）。
    JsonDocument req;
    req["text"] = text.c_str();
    std::string body;
    serializeJson(req, body);

    const int code = http.POST(String(body.c_str()));
    if (code != 200) { http.end(); return false; }

    // Content-Length。不明(-1)や中継方針(<=500KB)超過は安全側に弾く。
    const int len = http.getSize();
    constexpr size_t kMaxWav = 512 * 1024;
    if (len <= 0 || static_cast<size_t>(len) > kMaxWav) { http.end(); return false; }

    // 前回バッファを解放してから確保（PSRAM）。確保失敗もフォールバックへ。
    // playRaw は非同期でバッファをコピーしない（再生中は DMA が元バッファを参照し続ける）ため、
    // 解放の前に必ず停止する。止めずに free すると解放済み PSRAM を DMA が読む（use-after-free）。
    M5.Speaker.stop();
    if (g_ttsBuf) { free(g_ttsBuf); g_ttsBuf = nullptr; }
    g_ttsBuf = static_cast<uint8_t*>(ps_malloc(static_cast<size_t>(len)));
    if (!g_ttsBuf) { http.end(); return false; }

    // ストリームから len バイトを読み切る（available() を見ながら詰める）。
    WiFiClient* stream = http.getStreamPtr();
    size_t got = 0;
    while (got < static_cast<size_t>(len) && (http.connected() || stream->available())) {
        const size_t avail = stream->available();
        if (avail) {
            size_t want = static_cast<size_t>(len) - got;
            if (avail < want) want = avail;
            got += stream->readBytes(g_ttsBuf + got, want);
        } else {
            delay(1);  // まだ届いていない。少し待って再試行。
        }
    }
    http.end();
    if (got != static_cast<size_t>(len)) return false;

    // WAV ヘッダを剥がして PCM を再生（cry と共有の共通末尾）。
    return playWavBuffer(g_ttsBuf, got);
}

// ───────── 音声入力（録音→中継 /stt で文字起こし・P2 M3b-2 / Issue #55） ─────────
// 中継サーバの /stt エンドポイント URL を RELAY_URL（…/chat）から導出する（/tts と同じ作法）。
static std::string sttUrlFromRelay() {
    std::string u = RELAY_URL;
    const std::string chat = "/chat";
    if (u.size() >= chat.size() &&
        u.compare(u.size() - chat.size(), chat.size(), chat) == 0) {
        u.replace(u.size() - chat.size(), chat.size(), "/stt");
    }
    return u;
}

// 録音設定。16kHz/16bit/モノラルで固定長を録る（Whisper・/stt 推奨に合わせる）。
constexpr uint32_t kSttSampleRate    = 16000;
constexpr size_t   kSttRecordSamples = kSttSampleRate * 5 / 2;  // 2.5 秒ぶん

// 録音/送信で使い回すバッファ（PSRAM）。確保コストを避けるため一度だけ確保して保持する。
static int16_t* g_recPcm = nullptr;  // 録音した int16 PCM
static uint8_t* g_recWav = nullptr;  // write_wav 出力（44byteヘッダ + PCM）

// マイクで一定時間録音し、中継 /stt へ送って文字起こしを得る（実機依存部）。
// 成功時 true で out に認識テキスト（無音なら空文字もありうる）。失敗時 false（呼び出し側でフォールバック）。
static bool recordAndTranscribe(std::string& out) {
    if (WiFi.status() != WL_CONNECTED) return false;

    // PCM と WAV のバッファを一度だけ PSRAM に確保する。確保失敗は安全側に false。
    const size_t wavCap = wav_size(kSttRecordSamples * sizeof(int16_t));
    if (!g_recPcm) g_recPcm = static_cast<int16_t*>(ps_malloc(kSttRecordSamples * sizeof(int16_t)));
    if (!g_recWav) g_recWav = static_cast<uint8_t*>(ps_malloc(wavCap));
    if (!g_recPcm || !g_recWav) return false;

    // マイクとスピーカーは I2S を共有するため、録音前にスピーカーを止めてマイクを起こす。
    // ※ M5.Mic.begin / M5.Mic.end … 内部マイク(ES7210)の I2S を開始/停止する M5Unified の API。
    M5.Speaker.end();
    auto micCfg = M5.Mic.config();
    micCfg.sample_rate = kSttSampleRate;  // 16kHz をマイクにも明示（begin 前に渡す）
    M5.Mic.config(micCfg);
    if (!M5.Mic.begin()) { M5.Speaker.begin(); return false; }
    delay(150);  // 起動直後の飽和トランジェントを避けるための整定待ち

    // 録音は I2S 実時間で進む。record() は非同期にスロットへ積むだけで、isRecording() の
    // 立ち上がりにレースがある（呼んだ直後は 0 を返しうる＝録り切る前に読んでしまう）。
    // そこで「サンプル数÷サンプルレート」ぶん必ず待ってから、残りを isRecording() で排出する。
    M5.Mic.record(g_recPcm, kSttRecordSamples, kSttSampleRate);
    delay((uint32_t)kSttRecordSamples * 1000u / kSttSampleRate + 200);
    while (M5.Mic.isRecording()) { delay(1); }

    // マイクを閉じてスピーカーを戻す（次のメェ/発話のため I2S を返す）。
    M5.Mic.end();
    M5.Speaker.begin();

    // 録音 PCM を WAV にラップする（純粋ロジック・native テスト済み / Issue #53）。
    if (!write_wav(g_recWav, wavCap, g_recPcm, kSttRecordSamples, kSttSampleRate)) return false;

    // /stt へ raw WAV を POST。言語/タスクはサーバ既定（ja / transcribe）に委ねる。
    HTTPClient http;
    http.begin(sttUrlFromRelay().c_str());
    http.addHeader("Content-Type", "audio/wav");
    // Whisper の CPU 推論は数秒かかることがあるため、既定5秒では読み取りタイムアウト(-11)する。
    http.setTimeout(20000);  // 20秒まで待つ
    const int code = http.POST(g_recWav, wavCap);
    if (code != 200) { http.end(); return false; }

    const String payload = http.getString();
    http.end();

    // 応答 JSON {text} を解析。壊れていれば false（フォールバックへ）。
    JsonDocument res;
    if (deserializeJson(res, payload.c_str())) return false;
    out = std::string(res["text"] | "");
    return true;
}

// 認識テキストの簡易バナー（画面上部）。録音→STT の結果を数秒だけ見せるデバッグ表示（M3b-2）。
// 羊のクリップ枠は y>=44 なので、上部 y=8..34 に出せば描画が重ならない。
constexpr int      kSttBannerY  = 8;
constexpr int      kSttBannerH  = 26;
constexpr uint32_t kSttShowMs   = 4000;  // 表示してから消すまで
static uint32_t    g_sttShownMs = 0;     // バナーを出した時刻（0=非表示）

// バナーを描く/消す。和文を出すため drawDialog と同じ lgfxJapanGothic_16 を使う。
static void drawSttBanner(const std::string& text) {
    M5.Display.fillRect(0, kSttBannerY, kScreenW, kSttBannerH, kColBg);
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextColor(TFT_CYAN, kColBg);
    M5.Display.setCursor(4, kSttBannerY + 4);
    M5.Display.print(text.empty() ? "（聞き取れなかったのだ）" : text.c_str());
    M5.Display.setFont(&fonts::Font0);  // 既定に戻す（他描画への影響回避）
}
static void clearSttBanner() {
    M5.Display.fillRect(0, kSttBannerY, kScreenW, kSttBannerH, kColBg);
}

// 全身ドット羊を1フレーム描く。bob で上下に、shakeX で左右（タップ反応）に揺れ、目はまばたきする。
// 重なり描画なので、固定枠を背景色でクリアしてから毎フレーム全体を描き直す（最小実装）。
static void drawSheep(uint32_t now, int shakeX) {
    const int bob = sheep_bob_offset(now);
    // キャンバスはクリップ枠と同じ大きさ。枠の左上を原点とするローカル座標で描く。
    const int cx  = kSheepCx + shakeX - kSheepClipX;  // タップ反応で左右に揺らす
    const int cy  = kSheepCy + bob   - kSheepClipY;   // bob で全体を上下させる

    M5Canvas& cv = g_sheepCanvas;
    cv.fillScreen(kColBg);  // キャンバス全体（=クリップ枠）を背景色で初期化

    // 足（胴体の後ろに先に描く）。
    const int legY = cy + 26;
    for (int i = 0; i < 4; ++i) {
        cv.fillRect(cx - 30 + i * 20, legY, 8, 22, kColSheepLeg);
    }

    // モコモコ胴毛：白い円を重ねて雲状の輪郭を作り、中央を楕円で埋める。
    static const int8_t bumps[][2] = {
        {-46, -14}, {-26, -32}, {0, -38}, {26, -32}, {46, -14},
        {-52,   8}, { 52,   8}, {-30, 26}, {0, 32}, {30, 26},
    };
    for (auto& b : bumps) {
        cv.fillCircle(cx + b[0], cy + b[1], 22, kColWool);
    }
    cv.fillEllipse(cx, cy - 2, 52, 38, kColWool);

    // 耳（顔の左右、胴毛の前面）。
    cv.fillEllipse(cx - 26, cy + 8, 8, 11, kColEar);
    cv.fillEllipse(cx + 26, cy + 8, 8, 11, kColEar);

    // 顔（前面の下中央。胴毛の上に重ねる）。
    cv.fillEllipse(cx, cy + 8, 24, 20, kColSheepFace);

    // 目（まばたき）と鼻。
    const float eyeOpen = eye_openness(now);
    drawSheepEye(cv, cx - 9, cy + 3, eyeOpen);
    drawSheepEye(cv, cx + 9, cy + 3, eyeOpen);
    cv.fillTriangle(cx - 4, cy + 13, cx + 4, cy + 13, cx, cy + 18, kColSheepEye);

    // 完成したキャンバスを画面のクリップ枠位置へ一括転送（ここでだけ画面が更新される）。
    cv.pushSprite(&M5.Display, kSheepClipX, kSheepClipY);
}

// ───────── アートシーン（Issue #42 / #34 M3：フローフィールド曲線のアニメ） ─────────
// ノイズの流れ場(art_flow_angle)に沿って細い曲線を多数流す（Tyler Hobbs "Fidenza" 系）。
// 毎フレーム全描画してもちらつかないよう、フルスクリーンの M5Canvas に描いてから一括転送する。
constexpr int   kFlowLines = 120;    // 曲線の本数
constexpr int   kFlowSteps = 34;     // 1本あたりの積分ステップ数（曲線の長さ）
constexpr float kFlowStep  = 7.0f;   // 1ステップの進み(px)
constexpr float kFlowDt    = 0.005f; // 1フレームで時間 t を進める量（流れの変形速度）

// アート用フルスクリーンキャンバス。サイズが大きい(320x240x2≒150KB)ので PSRAM に確保する。
static M5Canvas g_artCanvas(&M5.Display);

// 1フレーム分のフローフィールドを描く。seed が配色と流れ、t が時間（変形）を決める。
static void drawFlowField(uint32_t seed, float t) {
    M5Canvas& cv = g_artCanvas;
    cv.fillScreen(art_flow_background(seed));

    // 始点は seed から決定的にばらまく（軽量 LCG）。各始点から角度場に沿って積分し細線で描く。
    uint32_t rng = seed * 2654435761u + 1u;
    auto nextf = [&rng]() {                 // [0,1) の擬似乱数を返すローカル関数
        rng = rng * 1664525u + 1013904223u;
        return static_cast<float>(rng >> 8) / 16777216.0f;
    };
    for (int i = 0; i < kFlowLines; ++i) {
        float x = nextf() * kArtScreenW;
        float y = nextf() * kArtScreenH;
        const uint16_t col = art_flow_color(seed, i);
        for (int s = 0; s < kFlowSteps; ++s) {
            const float ang = art_flow_angle(x, y, t, seed);  // その地点で進む向き
            const float nx  = x + cosf(ang) * kFlowStep;
            const float ny  = y + sinf(ang) * kFlowStep;
            // 1px だと細すぎるので2本重ねて約2pxの繊細な線にする。
            cv.drawLine(static_cast<int>(x), static_cast<int>(y),
                        static_cast<int>(nx), static_cast<int>(ny), col);
            cv.drawLine(static_cast<int>(x), static_cast<int>(y) + 1,
                        static_cast<int>(nx), static_cast<int>(ny) + 1, col);
            x = nx; y = ny;
            if (x < 0 || x >= kArtScreenW || y < 0 || y >= kArtScreenH) break;  // 画面外で打ち切り
        }
    }
    cv.pushSprite(&M5.Display, 0, 0);
}

// ───────── 宝石図鑑シーン（テーマ N / epic #27・P3 / Issue #66） ─────────
// 「タップで開くカード」の最初の具体形。純粋データ層 gem（#64）を実機画面に出す。
// 宝石スプライトは画像アセットを持たず、gem->color を明暗にシェードしてカット面を手続き生成する。
// カードは動かないので静的描画（enter/タップ時だけ描き、毎フレームは描かない）。

// 宝石3D表示の配置（rotation(1) の 320x240。上段中央に置き、下段に情報テキストを敷く）。
// 回転宝石は専用の小スプライトに毎フレーム描いて push する（カード本体は静止＝再描画しない）。
constexpr int kGemCx   = kScreenW / 2;  // 3D宝石の中心X（画面座標）
// 中心Y。スプライトは不透明な黒矩形で push されるため、その下端(kGemCy+kGemHalf)が
// 名前テキスト上端(=kGemNameY-12)に届くと文字の頭を毎フレーム消す。62+58=120 で 124 の手前に収める。
constexpr int kGemCy   = 62;            // 3D宝石の中心Y（黒矩形下端120 < 名前上端124）
constexpr int kGemHalf = 58;            // 回転スプライトの半辺（116x116 の正方）。投影拡大も収まる
constexpr float kGemR  = 42.0f;         // 投影正規化座標→画素のスケール（girdle 相当の見かけ半径）
constexpr float kGemCamD = 5.0f;        // 透視投影の焦点距離（z∈[-1,1]の単位メッシュで安全な距離）

// RGB565 を明暗にシェードする（pct=100 で原色、>100 で明るく、<100 で暗く）。
// 565 から r5/g6/b5 を取り出して各チャンネルを pct 倍し、飽和クランプして詰め直す（純粋な色演算）。
// 3D面のフラットシェーディング（面ごとの明るさ%）に使う。
static uint16_t gemShade(uint16_t c, int pct) {
    int r = (c >> 11) & 0x1F;
    int g = (c >> 5)  & 0x3F;
    int b =  c        & 0x1F;
    r = r * pct / 100; if (r > 31) r = 31;
    g = g * pct / 100; if (g > 63) g = 63;
    b = b * pct / 100; if (b > 31) b = 31;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// カード本体（背景＋テキスト）用のフルスクリーン・キャンバス（羊/アートと同じ作法）。
// 宝石送り時だけ描き替え一括 push する。フルスクリーン(320x240x2≒150KB)なので PSRAM に確保する。
static M5Canvas g_gemCanvas(&M5.Display);

// 回転宝石だけを描く小スプライト（116x116x2≒27KB）。毎フレーム描き替えるので、
// 速い内蔵RAMに置く（PSRAM 指定しない＝fillTriangle が速い）。カード本体の上に push する。
static M5Canvas g_gem3dCanvas(&M5.Display);

// 光源（宝石が向く方向＝面法線がこちらを向くほど明るい）。math座標(y上/+xが右)。
// z=-1 はカメラ側（手前）、+y/+x で右上から差す。実機で見栄えを微調整する係数。
static const Vec3 kGemLight{0.45f, 0.55f, -1.0f};

// ローポリ宝石を1フレーム分、スプライト spr に描く（フラットシェーディング・ソフトレンダ）。
//   spr の中心を宝石の中心とし、メッシュ mesh をオイラー角 (ax,ay,az) で回して投影し、
//   裏面カリング→奥行きソート（ペインターズ）→面ごとの明暗で fillTriangle する。
//   色 color（RGB565）を面の明るさでシェードして宝石色を反映する。
//   3D数学は純粋ロジック層 gem3d に委譲し、ここは描画（実機依存）だけを担う。
static void drawGem3d(M5Canvas& spr, const Mesh& mesh,
                      float ax, float ay, float az, uint16_t color) {
    const int cx = spr.width()  / 2;
    const int cy = spr.height() / 2;

    // 容量上限（八面体は6頂点/8面）。将来のローポリ拡張に少し余裕を持たせた固定長バッファ。
    constexpr int kMaxV     = 16;  // 頂点バッファ rot/proj/ok の長さ
    constexpr int kMaxFaces = 32;  // 可視面バッファ faces の長さ
    // 容量を超えるメッシュは固定長バッファを溢れさせる（面ループが頂点インデックスで添字するため
    // vcount だけでなく実インデックスも kMaxV 未満が前提）。安全側に描画を諦める。
    if (mesh.vcount > kMaxV || mesh.tcount > kMaxFaces) return;

    // 1) 全頂点を回して投影する。投影不能（カメラ手前 denom<=0）はフラグで弾く（M1申し送り1）。
    //    単位メッシュ＋d=5 では発火しないが、面が中心に潰れる事故を運用として防ぐ。
    Vec3 rot[kMaxV];
    Vec2 proj[kMaxV];
    bool ok[kMaxV];
    for (int i = 0; i < mesh.vcount && i < kMaxV; ++i) {
        rot[i]  = gem3d_rotate(mesh.verts[i], ax, ay, az);
        proj[i] = gem3d_project(rot[i], kGemCamD);
        ok[i]   = (kGemCamD + rot[i].z) > 0.0f;  // 投影可能か（gem3d_project と同じ判定）
    }

    // 2) 表向きの面だけ集め、代表深さで奥→手前にソートする。
    //    カリングは投影座標（math座標・y上）で行い、M1テストの巻き順規約と整合させる（申し送り2）。
    struct Face { int tri; float depth; };
    Face faces[kMaxFaces];
    int fn = 0;
    for (int i = 0; i < mesh.tcount && fn < kMaxFaces; ++i) {
        const Tri& t = mesh.tris[i];
        if (!ok[t.a] || !ok[t.b] || !ok[t.c]) continue;            // 投影不能頂点を含む面は捨てる
        if (gem3d_is_backface(proj[t.a], proj[t.b], proj[t.c])) continue;  // 裏面カリング
        faces[fn].tri   = i;
        faces[fn].depth = gem3d_face_depth(rot[t.a], rot[t.b], rot[t.c]);
        ++fn;
    }
    // 挿入ソートで深さ降順（z大＝奥が先）。面数 ≤ 8 なので軽い。
    for (int i = 1; i < fn; ++i) {
        Face key = faces[i];
        int j = i - 1;
        while (j >= 0 && faces[j].depth < key.depth) { faces[j + 1] = faces[j]; --j; }
        faces[j + 1] = key;
    }

    // 3) 奥から塗る。面法線×光で明るさ[0,1]を出し、環境光を足してシェード率に変換する。
    //    画面は y 下向きなので投影 y を反転して画素座標へ（カリングは上で math 座標済み＝符号一貫）。
    for (int k = 0; k < fn; ++k) {
        const Tri& t = mesh.tris[faces[k].tri];
        Vec3  n   = gem3d_face_normal(rot[t.a], rot[t.b], rot[t.c]);
        float br  = gem3d_face_brightness(n, kGemLight);   // [0,1]：1=正面から受光
        int   pct = 45 + static_cast<int>(br * 115.0f);    // 環境光45% + 拡散最大115% → [45,160]
        uint16_t col = gemShade(color, pct);

        const int axp = cx + static_cast<int>(proj[t.a].x * kGemR);
        const int ayp = cy - static_cast<int>(proj[t.a].y * kGemR);  // y反転（math上→画面下）
        const int bxp = cx + static_cast<int>(proj[t.b].x * kGemR);
        const int byp = cy - static_cast<int>(proj[t.b].y * kGemR);
        const int cxp = cx + static_cast<int>(proj[t.c].x * kGemR);
        const int cyp = cy - static_cast<int>(proj[t.c].y * kGemR);
        spr.fillTriangle(axp, ayp, bxp, byp, cxp, cyp, col);
    }
}

// 情報テキストの基準（中央寄せで段組み）。和文は lgfxJapanGothic を使う。
// 名前はスプライト先端(kGemCy+kGemPavilionH=118)より十分下から始め、稜線と被らせない。
constexpr int kGemNameY = 136;  // 名前（大・中心Y）
constexpr int kGemInfoY = 164;  // 以降の明細をこの間隔で並べる
constexpr int kGemInfoStep = 23;

// 宝石カードを1枚 gfx（画面 or キャンバス）へ描く（スプライト＋名前＋産地/組成/元素＋通し番号）。
// 背景クリアも gfx に対して行うので、呼び出し側でのクリアは不要。
static void drawGemCard(LovyanGFX& gfx, const Gem& g, int index, int total) {
    gfx.fillScreen(kColBg);  // キャンバス全体を背景色で初期化（画面なら全消去）

    // ヘッダ（図鑑名・左上）。
    gfx.setFont(&fonts::lgfxJapanGothic_16);
    gfx.setTextColor(TFT_CYAN, kColBg);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.drawString("宝石図鑑", 6, 4);
    // 通し番号（右上）。色は前段に依存させず明示する。
    gfx.setFont(&fonts::Font0);
    gfx.setTextColor(TFT_CYAN, kColBg);
    gfx.setTextSize(2);
    gfx.setTextDatum(textdatum_t::top_right);
    char idx[12];
    snprintf(idx, sizeof(idx), "%d/%d", index + 1, total);
    gfx.drawString(idx, kScreenW - 6, 4);
    gfx.setTextSize(1);

    // 宝石本体（上段中央）は回転する 3D スプライトが毎フレーム上書きするので、ここでは描かない。
    // （背景色のまま残し、その上に g_gem3dCanvas を push する）

    // 名前（大・中央寄せ）。
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setFont(&fonts::lgfxJapanGothic_24);
    gfx.setTextColor(TFT_WHITE, kColBg);
    gfx.drawString(g.name, kGemCx, kGemNameY);

    // 明細（産地→組成→元素）を中央寄せで縦に並べる。
    gfx.setFont(&fonts::lgfxJapanGothic_16);
    gfx.setTextColor(TFT_WHITE, kColBg);
    const std::string lines[] = {
        std::string("産地: ") + g.locality,
        std::string("組成: ") + g.formula,
        std::string("元素: ") + g.elements,
    };
    for (int i = 0; i < 3; ++i) {
        gfx.drawString(lines[i].c_str(), kGemCx, kGemInfoY + i * kGemInfoStep);
    }

    // 後続の描画（他シーンの print/setCursor 系）に影響しないよう既定へ戻す。
    gfx.setFont(&fonts::Font0);
    gfx.setTextDatum(textdatum_t::top_left);
}

// ───────── シーン状態機械（Issue #33：実行時シーン切替の基盤） ─────────
// 各シーンを「enter/update/onTap」の関数ポインタに揃え、配列で持つ。
// マネージャは現在の index を1つだけ握り、長押し(LongPress)で next_scene により次へ巡回、
// 短タップ(Tap)を現シーンの onTap に委譲する。
// シーンを増やすのは「配列に1個足すだけ」＝開放閉鎖の原則(SOLID)を満たす。
//
// ※ Face（中継サーバ対話）は Wi-Fi 依存で update(now) 内に非同期接続/HTTP を抱えるため、
//   本巡回には未組み込み。Face のシーン化は後続 Issue で扱う（描画関数群は温存）。
struct SceneDef {
    void (*enter)();              // 切替時に1回（背景クリア・初期描画）
    void (*update)(uint32_t now); // 毎フレーム描画
    void (*onTap)(uint32_t now);  // 短タップ反応
};

// --- 羊シーンの状態とアダプタ ---
bool     g_sheepTapped = false;  // 一度でもタップされたか（起動直後の誤発火を防ぐ）
uint32_t g_sheepTapMs  = 0;      // 直近タップの時刻（横揺れの起点）

// 発話状態（②-3a / Issue #23）。喋っている間だけ「喋り揺れ」を出すための起点と見積もり時間。
uint32_t g_sheepSpeakStart = 0;  // 喋り始めた時刻
uint32_t g_sheepSpeakDur   = 0;  // speaking_duration_ms の見積もり（0 なら喋っていない）

static void sheepEnter() {
    M5.Display.fillScreen(kColBg);
    // キャンバスは初回だけ確保（クリップ枠サイズ・約70KB）。getBuffer() が未確保なら作る。
    if (!g_sheepCanvas.getBuffer()) {
        g_sheepCanvas.createSprite(kSheepClipW, kSheepClipH);
    }
    g_sheepTapped = false;  // シーンに入り直したら揺れ状態をリセット
}
static void sheepUpdate(uint32_t now) {
    // 認識バナーの寿命管理：表示から一定時間が過ぎたら消す（一度だけ消去して描き直さない）。
    // ※ 引数 now は loop 先頭で取得した値で、録音＋通信で数秒ブロックした後はここに来る頃には
    //    古くなっている。g_sttShownMs（millis() で記録）との比較に古い now を使うと符号無し演算で
    //    桁あふれし、描いた直後に即消去されてしまうため、ここでは必ず最新の millis() で判定する。
    if (g_sttShownMs && millis() - g_sttShownMs > kSttShowMs) {
        clearSttBanner();
        g_sttShownMs = 0;
    }

    // 喋っている間は減衰しない「喋り揺れ」、喋り終えたらタップ反応の余韻、どちらも無ければ静止。
    int shakeX;
    if (is_speaking(now, g_sheepSpeakStart, g_sheepSpeakDur)) {
        shakeX = sheep_talk_offset(now - g_sheepSpeakStart);
    } else if (g_sheepTapped) {
        shakeX = sheep_shake_offset(now - g_sheepTapMs);
    } else {
        shakeX = 0;
    }
    drawSheep(now, shakeX);
}
static void sheepOnTap(uint32_t now) {
    g_sheepTapMs  = now;
    g_sheepTapped = true;

    // タップで鳴く。Wi-Fi があればクラウド TTS（ずんだもん）で喋り、失敗時は自前メェにフォールバック。
    // 「喋っている間だけ揺れる」ため、再生したものに応じて発話時間を見積もる（#23）。
    const std::string text = "メェ";
    if (speakTts(text)) {
        // クラウド TTS：実音長は不明なので返答(text)の長さから粗く見積もる。
        g_sheepSpeakDur = speaking_duration_ms(text.size());
    } else {
        playBleat();
        // フォールバックのメェ：実際の再生長（サンプル数 / サンプルレート）に合わせる。
        g_sheepSpeakDur = static_cast<uint32_t>(kBaaSamples) * 1000u / kVoiceSampleRate;
    }
    g_sheepSpeakStart = now;

    // メェの後にマイク録音→中継 /stt で文字起こしし、結果を上部バナーに出す（P2 M3b-2 / Issue #55）。
    // ここでスピーカー→マイクの I2S 切替を一度通す。失敗（オフライン等）なら何も表示しない。
    std::string heard;
    if (recordAndTranscribe(heard)) {
        Serial.printf("[stt] heard: %s\n", heard.c_str());
        drawSttBanner(heard);
        g_sttShownMs = millis();  // 録音に数秒かかるので now ではなく現在時刻を起点にする

        // 聞き取れた言葉を /chat(Claude) に渡し、返答をずんだもん声で喋り返す（対話ループ・#60）。
        // これで「タップ→喋りかけ→Claudeが考える→ずんだもん声で返答＋ダイアログ表示」が一巡する。
        ReplyMessage rep;
        if (!heard.empty() && fetchChatReply(heard, rep)) {
            Serial.printf("[chat] reply: %s\n", rep.reply.c_str());
            drawDialog(rep.reply, rep.expression);
            if (speakTts(rep.reply)) {
                // 録音＋/chat で数秒経つため、揺れ起点は古い now でなく最新 millis()（#58 と同じ理由）。
                g_sheepSpeakStart = millis();
                g_sheepSpeakDur   = speaking_duration_ms(rep.reply.size());
            }
        }
    }
}

// --- アートシーンの状態とアダプタ（フローフィールド・アニメ） ---
uint32_t g_artSeed = 1;     // 配色と流れを決めるシード
float    g_artT    = 0.0f;  // アニメ時間（毎フレーム進める＝流れがゆっくり変形）

static void artEnter() {
    g_artSeed = millis();  // 入るたびに違う作品（時刻をシードに）
    g_artT    = 0.0f;
    // フルスクリーンキャンバスを初回だけ PSRAM に確保（約150KB）。
    if (!g_artCanvas.getBuffer()) {
        g_artCanvas.setPsram(true);  // 内部RAMを圧迫しないよう PSRAM を使う
        g_artCanvas.createSprite(kArtScreenW, kArtScreenH);
    }
}
static void artUpdate(uint32_t /*now*/) {
    g_artT += kFlowDt;                 // 時間を進めて流れをゆっくり変形させる
    drawFlowField(g_artSeed, g_artT);
}
static void artOnTap(uint32_t /*now*/) {
    g_artSeed = millis();  // タップで配色・流れを一新
    g_artT    = 0.0f;
}

// --- 宝石図鑑シーンの状態とアダプタ（3D回転宝石・タップで次の宝石） ---
int   g_gemIdx = 0;     // 現在表示中の宝石番号（next_scene で巡回する）
float g_gemAngX = 0.3f; // 宝石の回転角（x軸・わずかに傾けて立体感を出す）
float g_gemAngY = 0.0f; // 宝石の回転角（y軸・主回転。毎フレーム進めて回す）

// y軸を主に回し、x軸はゆっくり傾ける（約30fps前提の1フレーム増分）。
constexpr float kGemSpinY = 0.045f;  // ≈1.35 rad/s → 1周およそ4.6秒
constexpr float kGemSpinX = 0.013f;  // ゆっくり首振り

// カード本体（背景＋名前＋明細）を描いて一括 push する。宝石送り時とシーン入場時だけ呼ぶ。
static void gemDrawCard() {
    const Gem* g = gem_at(g_gemIdx);
    if (!g) { M5.Display.fillScreen(kColBg); return; }  // 図鑑が空（通常起きない）の安全側
    drawGemCard(g_gemCanvas, *g, g_gemIdx, gem_count());
    g_gemCanvas.pushSprite(&M5.Display, 0, 0);
}
static void gemEnter() {
    // カード本体キャンバスは初回だけ PSRAM に確保（約150KB）。アートと同じ作法。
    if (!g_gemCanvas.getBuffer()) {
        g_gemCanvas.setPsram(true);
        g_gemCanvas.createSprite(kScreenW, kScreenH);
    }
    // 回転宝石スプライトは初回だけ内蔵RAMに確保（約27KB・毎フレーム描くので速いRAMに）。
    if (!g_gem3dCanvas.getBuffer()) {
        g_gem3dCanvas.createSprite(kGemHalf * 2, kGemHalf * 2);
    }
    gemDrawCard();  // シーンに入ったらカード本体を1回描く（以後 update が宝石だけ上書き）
}
static void gemUpdate(uint32_t /*now*/) {
    // 宝石を少し回してから、専用スプライトに描いてカード本体の上へ push する。
    g_gemAngY += kGemSpinY;
    g_gemAngX += kGemSpinX;
    const Gem* g = gem_at(g_gemIdx);
    if (!g) return;
    g_gem3dCanvas.fillScreen(kColBg);  // カード背景と同色でクリア（push 後に枠が出ないよう）
    drawGem3d(g_gem3dCanvas, gem3d_octahedron(), g_gemAngX, g_gemAngY, 0.0f, g->color);
    g_gem3dCanvas.pushSprite(&M5.Display, kGemCx - kGemHalf, kGemCy - kGemHalf);
}
static void gemOnTap(uint32_t /*now*/) {
    // 次の宝石へ。巡回は実装済みの next_scene を流用する（新規ロジックを作らない）。
    g_gemIdx = next_scene(g_gemIdx, gem_count());
    gemDrawCard();  // 名前・明細・通し番号を更新（宝石本体は直後の update が描く）
}

// ───────── ポケモン図鑑シーン（テーマ N / epic #27・P4 / Issue #80） ─────────
// 宝石図鑑と同形の「タップで開くカード」。ただしデータは著作物なので自前テーブルを持たず、
// 中継サーバから実行時取得する（/pokemon/info の JSON→構造体は純粋層 pokemon.cpp で担保済み）。
// スプライト画像もリポジトリに置かず、/pokemon/sprite が返す RGB565 raw を実行時に受けて表示する。
//
// 🔴 Pokémon and Pokémon character names are trademarks of Nintendo / Creatures Inc. / GAME FREAK inc.
//    情報・画像・鳴き声はいずれも実行時取得のみで、当リポジトリには一切コミットしない。

constexpr int    kPokeMaxId    = 151;                 // 循環範囲（カント地方の 1..151）
constexpr int    kPokeSprW     = 96;                  // スプライト幅（中継契約）
constexpr int    kPokeSprH     = 96;                  // スプライト高（中継契約）
constexpr size_t kPokeSprBytes = static_cast<size_t>(kPokeSprW) * kPokeSprH * 2;  // RGB565=18432B
constexpr int    kPokeSprX     = (kScreenW - kPokeSprW) / 2;  // 上段中央X（112）
constexpr int    kPokeSprY     = 18;                         // 上段Y（名前 kGemNameY=136 と被らない）
// 透過キー（中継契約）: この色の画素は描かずカード背景を透かす。マゼンタ 0xF81F(RGB565)。
constexpr uint16_t kPokeTransKey = 0xF81F;
// スプライト受信の全体タイムアウト。200＋Content-Length 後にストリームが中断しても
// available()==0 で delay を無限に回さず、締め切りで抜けて WDT リセットを防ぐ。
constexpr uint32_t kPokeFetchTimeoutMs = 8000;
// 説明文(desc_ja)を1行に収めるための最大表示文字数（全角想定・超過は … で丸める）。
// gem 明細は短い定数前提のレイアウトなので、長い図鑑説明はここで詰めてはみ出しを抑える。
constexpr int kPokeDescMaxChars = 18;

// UTF-8 文字列を先頭から maxChars「文字」で丸める（バイトではなく符号点で数え、多バイト境界を割らない）。
// 丸めが起きた時だけ末尾に … を足す。日本語の図鑑説明を1行に収めるために使う。
static std::string utf8_truncate(const std::string& s, int maxChars) {
    int chars = 0;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        // UTF-8 の先頭バイト長（0xxxxxxx=1 / 110=2 / 1110=3 / 11110=4）。継続バイトは飛ばして進む。
        size_t step = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        if (chars >= maxChars) return s.substr(0, i) + "…";  // ここまでで打ち切り
        i += step;
        ++chars;
    }
    return s;  // maxChars 以内なのでそのまま
}

// 中継のベースURL（末尾 /chat を除いたもの）。/tts /stt が /chat 前提なのと同じ導出。
static std::string relayBaseUrl() {
    std::string u = RELAY_URL;
    const std::string chat = "/chat";
    if (u.size() >= chat.size() &&
        u.compare(u.size() - chat.size(), chat.size(), chat) == 0) {
        u.erase(u.size() - chat.size());  // 末尾 /chat を落としてベースにする
    }
    return u;
}
static std::string pokeInfoUrl(int id)   { return relayBaseUrl() + "/pokemon/info/"   + std::to_string(id); }
static std::string pokeSpriteUrl(int id) { return relayBaseUrl() + "/pokemon/sprite/" + std::to_string(id); }
static std::string pokeCryUrl(int id)    { return relayBaseUrl() + "/pokemon/cry/"    + std::to_string(id); }

// カード本体（背景＋テキスト）用フルスクリーンキャンバス（宝石カードと同じ作法・PSRAM）。
static M5Canvas g_pokeCanvas(&M5.Display);
// 受信スプライト（RGB565 raw 18432B）。再取得のたびに再利用する。PSRAM に確保。
static uint8_t* g_pokeSprite    = nullptr;
int             g_pokeId        = 1;       // 現在表示中の図鑑番号（1..kPokeMaxId で循環）
Pokemon         g_poke;                    // 直近取得した情報（描画に使う）
bool            g_pokeHasSprite = false;   // スプライト取得に成功したか（失敗時は画像を出さない）
uint32_t        g_pokeTapMs     = 0;       // 直近タップ時刻（jiggle の減衰起点・#82）
bool            g_pokeJiggling  = false;   // jiggle 中か（終了時に中心へ戻す確定描画を1回だけ行う）

// GET してレスポンス本文を文字列で受ける（/pokemon/info 用・小さな JSON）。
static bool httpGetString(const std::string& url, std::string& out) {
    if (WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.begin(url.c_str());
    const int code = http.GET();
    if (code != 200) { http.end(); return false; }
    // info JSON は中継契約で <1KB。Content-Length が判れば上限超過を先に弾く（sprite 側と一貫）。
    const int len = http.getSize();
    constexpr int kMaxInfoBytes = 8 * 1024;
    if (len > kMaxInfoBytes) { http.end(); return false; }
    out = std::string(http.getString().c_str());
    http.end();
    return true;
}

// GET して RGB565 raw を g_pokeSprite に埋める（/pokemon/sprite 用・固定長 18432B）。
// speakTts の WAV 受信と同じストリーム読み。長さが契約(18432)と違えば安全側に弾く。
static bool fetchPokeSprite(int id) {
    if (WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.begin(pokeSpriteUrl(id).c_str());
    const int code = http.GET();
    if (code != 200) { http.end(); return false; }

    const int len = http.getSize();
    if (len != static_cast<int>(kPokeSprBytes)) { http.end(); return false; }  // 契約長のみ受理

    if (!g_pokeSprite) g_pokeSprite = static_cast<uint8_t*>(ps_malloc(kPokeSprBytes));
    if (!g_pokeSprite) { http.end(); return false; }

    WiFiClient* stream = http.getStreamPtr();
    size_t got = 0;
    const uint32_t deadline = millis() + kPokeFetchTimeoutMs;  // 全体締め切り
    while (got < kPokeSprBytes && (http.connected() || stream->available())) {
        const size_t avail = stream->available();
        if (avail) {
            size_t want = kPokeSprBytes - got;
            if (avail < want) want = avail;
            got += stream->readBytes(g_pokeSprite + got, want);
        } else if (static_cast<int32_t>(millis() - deadline) >= 0) {
            break;      // 締め切り超過（NW 中断など）→ 無限待ちせず抜ける
        } else {
            delay(1);   // まだ届いていない。少し待って再試行。
        }
    }
    http.end();
    return got == kPokeSprBytes;
}

// 鳴き声 WAV バッファ（g_ttsBuf と同パターン。playRaw 再生中も生存させ、次回先頭で解放）。PSRAM 使用。
static uint8_t* g_cryBuf = nullptr;

// 中継 /pokemon/cry/{id} から WAV を取得して鳴らす（実機依存部・#81）。
// speakTts の写経だが、POST body ではなく id 指定の GET。再生末尾は playWavBuffer を共有する。
// 失敗しても黙って鳴らさず図鑑閲覧を止めない（オフライン/上流失敗でもカードは見られる）。
static bool speakCry(int id) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    http.begin(pokeCryUrl(id).c_str());
    const int code = http.GET();
    if (code != 200) { http.end(); return false; }

    // Content-Length。cry は relay 契約で 16kHz mono 16bit（~30-64KB）。TTS と同じ上限で安全側に弾く。
    const int len = http.getSize();
    constexpr size_t kMaxWav = 512 * 1024;
    if (len <= 0 || static_cast<size_t>(len) > kMaxWav) { http.end(); return false; }

    // playRaw は非同期でバッファをコピーしないため、解放の前に必ず停止する（use-after-free 防止）。
    // cry はタップ連打で呼ばれやすく、前回の鳴き声が再生中/キュー待ちのまま free に至りやすい。
    M5.Speaker.stop();
    if (g_cryBuf) { free(g_cryBuf); g_cryBuf = nullptr; }
    g_cryBuf = static_cast<uint8_t*>(ps_malloc(static_cast<size_t>(len)));
    if (!g_cryBuf) { http.end(); return false; }

    // ストリームを締め切り付きで読み切る（sprite 取得と同じ作法。NW 中断でも WDT リセットを防ぐ）。
    WiFiClient* stream = http.getStreamPtr();
    size_t got = 0;
    const uint32_t deadline = millis() + kPokeFetchTimeoutMs;
    while (got < static_cast<size_t>(len) && (http.connected() || stream->available())) {
        const size_t avail = stream->available();
        if (avail) {
            size_t want = static_cast<size_t>(len) - got;
            if (avail < want) want = avail;
            got += stream->readBytes(g_cryBuf + got, want);
        } else if (static_cast<int32_t>(millis() - deadline) >= 0) {
            break;      // 締め切り超過 → 無限待ちせず抜ける
        } else {
            delay(1);   // まだ届いていない。少し待って再試行。
        }
    }
    http.end();
    if (got != static_cast<size_t>(len)) return false;

    // WAV ヘッダを剥がして PCM を再生（TTS と共有の共通末尾）。
    return playWavBuffer(g_cryBuf, got);
}

// ポケモンカードを1枚 gfx へ描く（宝石カード drawGemCard の写経・レイアウト定数を共用）。
// スプライトはカード push 後に別途 pushImage するので、ここでは上段領域を空けておく。
static void drawPokeCard(LovyanGFX& gfx, const Pokemon& p) {
    gfx.fillScreen(kColBg);

    // ヘッダ（図鑑名・左上）。
    gfx.setFont(&fonts::lgfxJapanGothic_16);
    gfx.setTextColor(TFT_CYAN, kColBg);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.drawString("ポケモン図鑑", 6, 4);
    // 図鑑番号（右上）。id/総数 で現在地を示す。
    gfx.setFont(&fonts::Font0);
    gfx.setTextColor(TFT_CYAN, kColBg);
    gfx.setTextSize(2);
    gfx.setTextDatum(textdatum_t::top_right);
    char idx[12];
    snprintf(idx, sizeof(idx), "%d/%d", p.id, kPokeMaxId);
    gfx.drawString(idx, kScreenW - 6, 4);
    gfx.setTextSize(1);

    // 名前（大・中央寄せ）。
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setFont(&fonts::lgfxJapanGothic_24);
    gfx.setTextColor(TFT_WHITE, kColBg);
    gfx.drawString(p.name_ja.c_str(), kGemCx, kGemNameY);

    // 明細（タイプ→分類→説明）を中央寄せで縦に並べる。
    gfx.setFont(&fonts::lgfxJapanGothic_16);
    gfx.setTextColor(TFT_WHITE, kColBg);
    const std::string lines[] = {
        std::string("タイプ: ") + p.types,
        std::string("分類: ") + p.category_ja,
        utf8_truncate(p.desc_ja, kPokeDescMaxChars),  // 長い図鑑説明は1行に収まるよう丸める
    };
    for (int i = 0; i < 3; ++i) {
        gfx.drawString(lines[i].c_str(), kGemCx, kGemInfoY + i * kGemInfoStep);
    }

    // 既定へ戻す（他シーンの描画に影響させない）。
    gfx.setFont(&fonts::Font0);
    gfx.setTextDatum(textdatum_t::top_left);
}

// スプライトを上段に描く（中心から dx px 横ずらし＝jiggle 用・#82）。
// 中継はメモリ上 LE で RGB565 を吐くが LovyanGFX はパネル既定（上位バイト先）でDMAするため、
// そのままだと色の上下バイトが入れ替わって化ける。push 直前に setSwapBytes(true) で送出時に
// スワップさせる（#80 実機確認済み）。pokeLoad と jiggle 双方から呼ぶので共通化しておく。
static void pokePushSprite(int dx) {
    if (!g_pokeHasSprite || !g_pokeSprite) return;
    M5.Display.setSwapBytes(true);
    M5.Display.pushImage(kPokeSprX + dx, kPokeSprY, kPokeSprW, kPokeSprH,
                         reinterpret_cast<const uint16_t*>(g_pokeSprite), kPokeTransKey);
    M5.Display.setSwapBytes(false);  // 他シーンの描画に影響しないよう既定へ戻す
}

// 現在 id の情報＋スプライトを取得して1枚描く（シーン入場・タップ送りの両方から呼ぶ）。
static void pokeLoad() {
    std::string json;
    if (httpGetString(pokeInfoUrl(g_pokeId), json)) {
        g_poke = parse_pokemon_info(json);
    } else {
        g_poke = Pokemon{};             // 取得失敗 → 空にして番号だけ見せる
        g_poke.id      = g_pokeId;
        g_poke.name_ja = "取得失敗";
    }
    g_pokeHasSprite = fetchPokeSprite(g_pokeId);

    drawPokeCard(g_pokeCanvas, g_poke);
    g_pokeCanvas.pushSprite(&M5.Display, 0, 0);
    pokePushSprite(0);  // カード上段中央にスプライトを重ねる（中心＝dx 0）
}

static void pokeEnter() {
    // カード本体キャンバスは初回だけ PSRAM に確保（約150KB・宝石/アートと同じ作法）。
    if (!g_pokeCanvas.getBuffer()) {
        g_pokeCanvas.setPsram(true);
        g_pokeCanvas.createSprite(kScreenW, kScreenH);
    }
    pokeLoad();  // 入場時に現在 id を取得して描く
}
static void pokeUpdate(uint32_t /*now*/) {
    // 通常は静止画（取得済みの1枚を出しっぱなし）。タップ直後だけ jiggle で毎フレーム描く（#82）。
    // スプライトが無ければ揺らすものが無い＝静止画のまま（無駄な全画面再描画も避ける）。
    if (!g_pokeJiggling || !g_pokeHasSprite) return;
    // 引数 now は loop 先頭の値で、pokeOnTap 内の pokeLoad(HTTP)で数秒ブロックした後はここでは古い。
    // g_pokeTapMs（millis()で記録）との差に古い now を使うと符号無し演算で桁あふれし即終了するため、
    // sheepUpdate と同じく必ず最新の millis() で判定する。
    const uint32_t elapsed = millis() - g_pokeTapMs;
    const int dx = sheep_shake_offset(elapsed);  // 時間→減衰する横揺れ（純粋関数・test_sheep で検証済み）
    // カード全体を描き直してからスプライトを dx ずらして重ねる＝残像（trail）を残さない。
    g_pokeCanvas.pushSprite(&M5.Display, 0, 0);
    pokePushSprite(dx);
    // sheep_shake_offset は減衰しきると 0 を返す。その最終フレーム（dx=0＝中心）を描いてから静止へ戻す。
    if (elapsed >= kShakeDurationMs) g_pokeJiggling = false;
}
static void pokeOnTap(uint32_t /*now*/) {
    g_pokeId = (g_pokeId % kPokeMaxId) + 1;  // 1..kPokeMaxId を循環（151 の次は 1）
    pokeLoad();
    speakCry(g_pokeId);  // 送った先のポケモンの鳴き声を鳴らす（#81。失敗しても図鑑閲覧は続く）
    // jiggle 起点は取得・再生の後に実時刻で打つ。pokeLoad の HTTP ブロッキング分ずれず、鳴き声と同期する。
    g_pokeTapMs    = millis();
    g_pokeJiggling = true;
}

// シーン表（巡回順）。ここに1要素足すだけで新テーマを増やせる。
const SceneDef kScenes[] = {
    { sheepEnter, sheepUpdate, sheepOnTap },
    { artEnter,   artUpdate,   artOnTap   },
    { gemEnter,   gemUpdate,   gemOnTap   },
    { pokeEnter,  pokeUpdate,  pokeOnTap  },
};
constexpr int kSceneCount = static_cast<int>(sizeof(kScenes) / sizeof(kScenes[0]));
int g_sceneIdx = 0;  // 現在のシーン番号（next_scene で巡回する）

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);  // 診断ログ用（M3b-2 デバッグ）
    Serial.println("\n[boot] m5-cores3-lite up");
    M5.Display.setRotation(1);
    M5.Display.fillScreen(kColBg);

    // メェの PCM を一度だけ合成しておく（毎タップで作り直さない）。
    g_baaLen = voice_baa_pcm(g_baaPcm, kBaaSamples);

    // Wi-Fi 接続をノンブロッキングで開始する（待たない）。
    // タップ時点で繋がっていればクラウド TTS、未接続なら自前メェにフォールバックする。
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    g_wifiBeginMs = millis();

    kScenes[g_sceneIdx].enter();  // 初期シーンを描き始める
}

void loop() {
    M5.update();
    const uint32_t now = millis();

    // タッチ系列をジェスチャ検出に流し、短タップ/長押しを判定する（純粋ロジック）。
    static TouchTracker tracker;
    const bool touching = (M5.Touch.getCount() > 0);
    const TouchEvent ev = touch_update(tracker, touching, now);

    if (ev == TouchEvent::LongPress) {
        // 長押し → 次シーンへ巡回し、新シーンを初期描画する。
        g_sceneIdx = next_scene(g_sceneIdx, kSceneCount);
        kScenes[g_sceneIdx].enter();
    } else if (ev == TouchEvent::Tap) {
        // 短タップ → 現シーンの反応に委譲（羊のメェ／アート再生成など）。
        kScenes[g_sceneIdx].onTap(now);
    }

    kScenes[g_sceneIdx].update(now);  // 毎フレーム描画
    delay(33);  // 約30fps
}
