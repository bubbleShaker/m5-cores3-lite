#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>   // 中継サーバへの HTTP POST（ESP32 標準）
#include <ArduinoJson.h>  // リクエスト body の安全な組み立て
#include <string>
#include <vector>
#include <math.h>     // cosf / sinf（流れ場に沿った曲線の積分に使う）
// 顔の純粋ロジック（表情/まばたき/口パク）。M5Stack-Avatar が Avatar.h / Face.h / Expression.h を
// 持ち、Windows の case-insensitive FS ではそれらと同名のヘッダを src/ に置けないため、
// ライブラリのどのヘッダとも衝突しない名前にしてある（#126。avatar.h → face.h → face_logic.h）。
#include "face_logic.h"
#include "sheep.h"
#include "art.h"      // art_generate（幾何学アートの図形プリミティブ生成・純粋ロジック）
#include "gesture.h"  // touch_update（短タップ/長押し検出・純粋ロジック）
#include "scene.h"    // next_scene（シーン巡回・純粋ロジック）
#include "menu.h"     // menu_move（メニューのカーソル移動・純粋ロジック・#132）
#include "voice.h"    // voice_baa_pcm（メェの PCM 合成・純粋ロジック）
#include "gem.h"      // Gem / gem_count / gem_at（宝石図鑑データ・純粋ロジック）
#include "gem3d.h"    // gem3d_*（宝石3D回転の純粋数学：回転/投影/カリング/法線/明るさ）
#include "wav.h"      // parse_wav_header（/tts の WAV から PCM を取り出す・純粋ロジック）
#include "net.h"
#include "pokemon.h"  // Pokemon / parse_pokemon_info（/pokemon/info の JSON→構造体・純粋ロジック）
#include "commentary.h"  // gem_commentary / pokemon_commentary（カード→ずんだもん語の解説文・純粋ロジック）
#include "volume.h"   // volume_up/down / volume_to_speaker / volume_is_up_tap（音量調整・純粋ロジック）
#include "voice_select.h"  // voice_speaker_at / voice_name_at / voice_next 等（話者選択・純粋ロジック・#105）
#include "envelope.h"   // voice_envelope（発話 PCM→音量エンベロープ・純粋ロジック・#109）
#include "particles.h"  // particle_ring（円形パーティクルの幾何・純粋ロジック・#109）
#include "pacman.h"   // Tile/Dir/Pos / pac_tile_at / pac_can_move / pac_step（パックマン迷路・純粋ロジック・#134）
#include "video.h"    // video_frame_at（動画フレーム時刻・純粋ロジック・#142）
#include "secrets.h"  // WIFI_SSID / WIFI_PASS / RELAY_URL（git管理外。secrets.h.example を参照）

// 定番OSS「スタックチャン」の顔（#121 で lib_deps 追加 / #125 でシーン化）。
// ライブラリも Expression という型を持ち自作 face_logic.h と同名なので、名前空間は開かず
// m5avatar:: で明示的に修飾する。
#include <Avatar.h>

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

// 音声読み上げ ON/OFF（#132）。既定 OFF＝音声のためのサーバー通信を最初から行わず、読み上げ導入
// 前の表示速度に戻す。メニュー先頭行のトグルで切り替える。OFF の間、音声を出す/対話する全経路
// （TTS 合成/先読み・鳴き声・メェ・録音→/stt・/chat）は入口で即 return し、HTTP を一切張らない。
// ポケモンの info/sprite は「絵の表示に必須・音声導入前から必要」な通信なので、ここでは遮断しない。
// fetchChatReply より前に宣言する（同関数の入口ガードが参照するため）。
static bool g_voiceEnabled = false;

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
    if (!g_voiceEnabled) return false;  // 音声 OFF：/chat 通信を張らない（#132・OFF=通信ゼロを独立保証）
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

// 現在の音量レベル（0〜kVolumeMax）。音量シーンの左右タップで増減し、
// 全再生経路（メェ／TTS／鳴き声）がこの1つの状態を参照する（音量の一元管理・DRY）。
static int g_volumeLevel = kVolumeDefault;

// 現在の話者選択インデックス（#105）。設定シーンの左右タップで巡回し、TTS 全経路
// （fetchTtsWav）がこの1つの状態から speaker id を引く。既定 0=ずんだもんで従来の声を維持。
static int g_voiceIdx = 0;

// 現在の音量レベルを実機スピーカーへ反映する。再生の直前に必ず呼ぶ。
static void applyVolume() {
    M5.Speaker.setVolume(volume_to_speaker(g_volumeLevel));
}

// 「メェ」を鳴らす。tone() の単音ではなく、合成した波形(PCM)を playRaw で再生する
// （クラウド TTS でもサーバが返す PCM をこの経路で鳴らす＝同じ playRaw を先に通す）。
static void playBleat() {
    if (!g_voiceEnabled) return;  // 音声 OFF：鳴らさない（#132）
    applyVolume();
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

// 発話中パーティクル用の音量エンベロープと再生タイミング（#109）。playWavBuffer が再生開始時に
// 埋め、ひつじシーンの描画が「経過時間→bucket→強さ」で実音声に連動したリングを描く。
constexpr int   kEnvBuckets    = 24;
static uint8_t  g_voiceEnv[kEnvBuckets];
static int      g_voiceEnvN     = 0;   // g_voiceEnv の有効数（0=無効）
static uint32_t g_voicePlayStart = 0;  // 再生開始 millis()
static uint32_t g_voicePlayDur   = 0;  // 再生長 ms（0=無効）

// 受信済み WAV バッファから PCM 本体を取り出して playRaw で鳴らす共通末尾。
// TTS(/tts) と鳴き声(/pokemon/cry) は「取得の仕方」だけ違い、ヘッダ剥がし→再生は同一なので
// ここに集約する（speakTts / speakCry が共有・DRY）。parse_wav_header は native テスト済みの純粋層。
// volumeScale は「この再生だけ音量を控えめにする」倍率（0.0〜1.0・既定1.0＝現在の音量そのまま）。
// 鳴き声(cry)を読み上げ(TTS)より小さくするために使う（#99）。音量レベル g_volumeLevel 自体は変えない
// ので、次の再生（scale=1.0）で自動的に通常音量へ戻る。
static bool playWavBuffer(const uint8_t* buf, size_t len, float volumeScale = 1.0f) {
    WavInfo info;
    if (!parse_wav_header(buf, len, &info)) return false;

    const int16_t* pcm = reinterpret_cast<const int16_t*>(buf + info.data_offset);
    const size_t samples = info.data_bytes / 2;  // 16bit = 2byte / サンプル
    // applyVolume() の setVolume 一段だけを差し替え、現在の音量(0〜255)へ倍率を掛けて出す。
    // float→uint8_t は範囲外だと未定義動作なので、倍率を 0.0〜1.0 に丸めてから掛ける（誤用防止）。
    // 「>= 0.0f でない」で下限を判定するので、NaN も比較が false になりこの枝で 0.0f に落ちる。
    const float scale = !(volumeScale >= 0.0f) ? 0.0f : (volumeScale > 1.0f ? 1.0f : volumeScale);
    M5.Speaker.setVolume(static_cast<uint8_t>(volume_to_speaker(g_volumeLevel) * scale));
    const bool ok = M5.Speaker.playRaw(pcm, samples, info.sample_rate, info.channels == 2);

    // 再生開始後に音量エンベロープと再生長を記録する（#109）。playRaw は非同期なので、ここでの
    // 読み取り専用スキャンは DMA と競合しない。再生開始そのものは遅らせない（音の頭出しに影響なし）。
    if (ok) {
        g_voiceEnvN = voice_envelope(pcm, static_cast<int>(samples), g_voiceEnv, kEnvBuckets);
        const uint32_t rate   = info.sample_rate ? info.sample_rate : 1;
        const uint32_t frames  = (info.channels == 2) ? static_cast<uint32_t>(samples / 2)
                                                       : static_cast<uint32_t>(samples);
        g_voicePlayDur   = static_cast<uint32_t>(static_cast<uint64_t>(frames) * 1000u / rate);
        g_voicePlayStart = millis();
    }
    return ok;
}

// 受信した WAV を置く「再生中バッファ」。playRaw 再生中も生かしておく必要があるため static で
// 保持し、次回再生の直前で解放する（再生中に free しない）。容量が読めないので PSRAM を使う。
static uint8_t* g_ttsBuf = nullptr;

// 先読み(prefetch)スロット（#101）。カード巡回は決定的（next_scene / id+1）なので、次に喋る
// 解説文の WAV をタップ前に取得して置いておく。タップ時に text が一致すれば合成を待たず即再生でき、
// タップ→発話の critical path から合成待ちが消える。再生には使わない待機用の別バッファ。
static uint8_t*   g_prefetchBuf  = nullptr;
static size_t     g_prefetchLen  = 0;
static std::string g_prefetchText;    // g_prefetchBuf が保持している WAV のテキスト（空＝未保持）
static int         g_prefetchVoice = -1;  // 保持している WAV を合成した speaker id（声変更で無効化するため一致条件に含める・#105）

// 中継 /tts へ text を投げ、返ってきた WAV を新規 PSRAM バッファへ読み切る（取得だけ・再生しない）。
// 成功時は *outBuf に確保済みバッファ（呼び出し側が free 責任を持つ）と *outLen を返す。
// speakTts（再生用）と prefetchTts（先読み用）で取得ロジックを共有する（DRY）。
static bool fetchTtsWav(const std::string& text, uint8_t** outBuf, size_t* outLen) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    http.begin(ttsUrlFromRelay().c_str());
    http.addHeader("Content-Type", "application/json");

    // body は ArduinoJson で組み立て（特殊文字を安全にエスケープ）。
    // voice_id は現在の話者選択から引く（未指定なら中継が既定=ずんだもんに倒すが、明示する・#105）。
    JsonDocument req;
    req["text"]     = text.c_str();
    req["voice_id"] = voice_speaker_at(g_voiceIdx);
    std::string body;
    serializeJson(req, body);

    const int code = http.POST(String(body.c_str()));
    if (code != 200) { http.end(); return false; }

    // Content-Length。不明(-1)や上限超過は安全側に弾く。
    // 24kHz 化(#107)で同じ文字数でも WAV が約1.5倍になるため、長文解説が新たに無音落ちしないよう
    // 上限を 1MB に引き上げる（PSRAM 8MB に対し先読みと合わせ最大2MBで十分収まる）。
    const int len = http.getSize();
    constexpr size_t kMaxWav = 1024 * 1024;
    if (len <= 0 || static_cast<size_t>(len) > kMaxWav) { http.end(); return false; }

    // 取得専用の新規バッファへ確保（再生中の g_ttsBuf には触れない＝use-after-free を避ける）。
    uint8_t* buf = static_cast<uint8_t*>(ps_malloc(static_cast<size_t>(len)));
    if (!buf) { http.end(); return false; }

    // ストリームから len バイトを読み切る（available() を見ながら詰める）。
    // speakCry と同じく全体締め切りを張る。上流が「接続維持のまま無応答」でも delay(1) で無限
    // スピンせず抜ける（先読みで同期取得の呼び出し箇所が増えるため、loop の WDT リセットを防ぐ）。
    WiFiClient* stream = http.getStreamPtr();
    constexpr uint32_t kTtsFetchTimeoutMs = 8000;  // 受信全体の締め切り（speakCry と同値）
    const uint32_t deadline = millis() + kTtsFetchTimeoutMs;
    size_t got = 0;
    while (got < static_cast<size_t>(len) && (http.connected() || stream->available())) {
        const size_t avail = stream->available();
        if (avail) {
            size_t want = static_cast<size_t>(len) - got;
            if (avail < want) want = avail;
            got += stream->readBytes(buf + got, want);
        } else if (static_cast<int32_t>(millis() - deadline) >= 0) {
            break;     // 締め切り超過（NW 中断など）→ 無限待ちせず抜ける
        } else {
            delay(1);  // まだ届いていない。少し待って再試行。
        }
    }
    http.end();
    if (got != static_cast<size_t>(len)) { free(buf); return false; }

    *outBuf = buf;
    *outLen = got;
    return true;
}

// 次に喋る解説文の WAV を先読みしてスロットに置く（#101）。同じ text を保持済みなら何もしない
// （二重取得を避ける）。取得は同期だが、カード描画直後の「ユーザーが眺めている余白」で呼ぶことで、
// タップ→発話の critical path から合成待ちを外す狙い。失敗時は黙ってスロットを空のままにする
// （フォールバックは speakTts 側が同期合成で担うので、鳴らないことはない）。
// 先読みスロットが「今から喋りたい text＋現在の話者」に一致するか（#101/#105）。
// 声を変えた後に旧声の WAV を再生しないよう、text だけでなく合成時の speaker も一致条件に含める。
static bool prefetchMatches(const std::string& text) {
    return g_prefetchBuf && g_prefetchText == text &&
           g_prefetchVoice == voice_speaker_at(g_voiceIdx);
}

static void prefetchTts(const std::string& text) {
    if (!g_voiceEnabled) return;  // 音声 OFF：先読み HTTP を張らない（#132）
    if (text.empty()) return;
    if (prefetchMatches(text)) return;  // 既に同じ text＋話者を保持

    // 取得前に現在の話者を確定させる（fetchTtsWav は g_voiceIdx から voice_id を載せる）。
    const int voice = voice_speaker_at(g_voiceIdx);
    uint8_t* buf = nullptr;
    size_t   len = 0;
    if (!fetchTtsWav(text, &buf, &len)) return;  // 失敗なら現状維持

    // 取得成功。古い先読みは破棄して差し替える。g_prefetchBuf は再生に使われていない
    // （speakTts が使う時は g_ttsBuf へ所有権を移す）ので、ここでの free は安全。
    if (g_prefetchBuf) free(g_prefetchBuf);
    g_prefetchBuf   = buf;
    g_prefetchLen   = len;
    g_prefetchText  = text;
    g_prefetchVoice = voice;
}

// 先読みスロットを「再生中バッファ」へ移譲して再生する（#101）。playRaw は非同期で元バッファを
// 参照し続けるため、再生に使うバッファは g_ttsBuf に一本化して寿命管理する。
static bool playPrefetched() {
    // DMA が旧 g_ttsBuf を参照中の可能性があるため、停止してから解放・差し替える。
    M5.Speaker.stop();
    if (g_ttsBuf) free(g_ttsBuf);
    g_ttsBuf         = g_prefetchBuf;
    const size_t len = g_prefetchLen;
    g_prefetchBuf    = nullptr;
    g_prefetchLen    = 0;
    g_prefetchVoice  = -1;
    g_prefetchText.clear();
    // WAV ヘッダを剥がして PCM を再生（cry と共有の共通末尾）。
    return playWavBuffer(g_ttsBuf, len);
}

// 中継 /tts に text を投げ、返ってきた WAV を playRaw で鳴らす（実機依存部・P2 M2b / Issue #48）。
// 成否を返し、失敗時は呼び出し側で自前メェにフォールバックさせる（オフラインでも必ず鳴る）。
// 先読み(#101)済みの text はサーバ合成を待たず即再生する。
static bool speakTts(const std::string& text) {
    if (!g_voiceEnabled) return false;  // 音声 OFF：合成 HTTP を張らず、呼び出し側は無音で続行（#132）
    // 先読みヒット（text＋話者一致）：合成待ちゼロで即再生（タップ→発話ラグ解消の要）。
    if (prefetchMatches(text)) {
        return playPrefetched();
    }

    // ミス：同期取得してから再生（従来経路）。
    uint8_t* buf = nullptr;
    size_t   len = 0;
    if (!fetchTtsWav(text, &buf, &len)) return false;

    // 再生バッファへ移す。playRaw は非同期でバッファをコピーしない（再生中は DMA が元バッファを
    // 参照し続ける）ため、旧バッファは必ず停止してから解放する。止めずに free すると解放済み
    // PSRAM を DMA が読む（use-after-free）。
    M5.Speaker.stop();
    if (g_ttsBuf) free(g_ttsBuf);
    g_ttsBuf = buf;

    // WAV ヘッダを剥がして PCM を再生（cry と共有の共通末尾）。
    return playWavBuffer(g_ttsBuf, len);
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
    if (!g_voiceEnabled) return false;  // 音声 OFF：録音も /stt 通信もしない（#132）
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

// ───────── 発話中の楕円素粒子エフェクト（Issue #109 / 全シーン共通化 #117） ─────────
// 主役（アバター/宝石/…）の周囲の背景に、実音声の音量エンベロープへ連動して脈動する楕円リングを描く。
// 幾何は純粋ロジック particle_ring、強さは voice_envelope が担当（native テスト済み）。中心・半径・
// 背景色・除外矩形を引数で受け、各シーンの update から自分のジオメトリで呼ぶ。背景が静止している
// シーン専用（前フレームの粒を背景色で塗り消す方式なので、毎フレーム全画面を描き直すシーンには使わない）。
// 楕円(横>縦)で横長のレイアウトを均等に囲む。真円だと左右が主役の枠に埋もれ下弧しか出なかった(#116)。
constexpr int kSpeakRingRadiusX = 116;  // 標準の横半径：羊枠右端 x=264 を越えるよう >104
constexpr int kSpeakRingRadiusY = 84;   // 標準の縦半径：羊枠下端 y=190 を越え、上はバナー(34)より下
constexpr int kMaxParticles     = 16;

// 発話中に避ける矩形（主役スプライトや文字帯）。この内側には粒を描かない。
struct ParticleExcl { int x, y, w, h; };
// 除外矩形配列の要素数を配列そのものから導く（呼び出し側で個数を手書きして更新漏れ→範囲外参照になるのを防ぐ）。
template <int N> constexpr int particleExclCount(const ParticleExcl (&)[N]) { return N; }

// 直前フレームに描いた粒子（次フレーム先頭で背景色に塗り消して trail を残さない＝全画面クリア不要）。
static Particle g_prevParticles[kMaxParticles];
static int      g_prevParticleN  = 0;
static uint16_t g_prevParticleBg = 0;  // 消去に使う背景色（描いた時点の色で消す＝シーンを跨いでも正しい）

// エンベロープの明るさ(0..255)を素粒子らしい水色系の色へ写す（暗→黒、明→白寄りの水色）。
static uint16_t particleColor(uint8_t level) {
    return M5.Display.color565(level >> 1, level, level);  // R控えめ・G/B強め＝シアン〜白
}

// シーン切替時に呼ぶ：前フレーム粒子を「捨てる」（別シーンの背景へ誤って塗り消さない）。
static void resetSpeakingParticles() { g_prevParticleN = 0; }

// 発話中パーティクルを1フレーム更新する。各シーンの update から中心(cx,cy)・楕円半径(rx,ry)・
// 背景色 bg・除外矩形 excl[exclN] を渡して毎フレーム呼ぶ。
static void drawSpeakingParticles(uint32_t now, int cx, int cy, int rx, int ry,
                                  uint16_t bg, const ParticleExcl* excl, int exclN) {
    // 1) まず直前フレームの粒を、描いた時点の背景色で消す（除外矩形外にしか描いていないので主役は触らない）。
    for (int i = 0; i < g_prevParticleN; ++i) {
        M5.Display.fillCircle(g_prevParticles[i].x, g_prevParticles[i].y,
                              g_prevParticles[i].radius, g_prevParticleBg);
    }
    g_prevParticleN = 0;

    // 2) 実際に鳴っていなければ（or エンベロープ無効なら）ここで終わり＝粒子は消えたまま。
    if (!M5.Speaker.isPlaying() || g_voicePlayDur == 0 || g_voiceEnvN <= 0) return;

    // 3) 経過時間を bucket に写して実音声の大小を強さ(0..1)にする（＝声に連動）。
    const uint32_t elapsed = now - g_voicePlayStart;
    int idx = static_cast<int>(static_cast<uint64_t>(elapsed) * g_voiceEnvN / g_voicePlayDur);
    if (idx < 0) idx = 0;
    if (idx >= g_voiceEnvN) idx = g_voiceEnvN - 1;
    const float intensity = g_voiceEnv[idx] / 255.0f;

    // 4) 楕円リング幾何を計算して、除外矩形の外側だけに描く（主役・文字を隠さない）。
    Particle ps[kMaxParticles];
    const float t = now / 1000.0f;
    const int n = particle_ring(ps, kMaxParticles, kMaxParticles,
                                cx, cy, rx, t, intensity, ry);
    g_prevParticleBg = bg;  // この回に描く粒は、次フレームで bg で消す
    for (int i = 0; i < n; ++i) {
        const int px = ps[i].x, py = ps[i].y;
        if (px < 0 || px >= kScreenW || py < 0 || py >= kScreenH) continue;  // 画面外
        bool inExcl = false;
        for (int e = 0; e < exclN; ++e) {
            const ParticleExcl& r = excl[e];
            if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) { inExcl = true; break; }
        }
        if (inExcl) continue;
        M5.Display.fillCircle(px, py, ps[i].radius, particleColor(ps[i].level));
        g_prevParticles[g_prevParticleN++] = ps[i];
    }
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
    const char* name;                         // メニューに出す表示名（#132）
    void (*enter)();                          // 切替時に1回（背景クリア・初期描画）
    void (*update)(uint32_t now);             // 毎フレーム描画
    void (*onTap)(uint32_t now, int touchX);  // 短タップ反応（touchX=離す直前のタッチX座標）
    // 切替で「出て行く」時に1回（省略可 = nullptr）。自前で描画タスクを持つシーン（スタックちゃん）が
    // 画面の占有を手放すために使う。持たないシーンは loop が描くだけなので nullptr でよい（#125）。
    void (*exit)();
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
    // 発話中だけアバター周囲に実音声連動の楕円パーティクル（#109 / 共通化 #117）。
    // 除外＝キャラのクリップ枠 と 上部の認識バナー帯(y<34)。背景色は kColBg。
    const ParticleExcl sheepExcl[] = {
        { kSheepClipX, kSheepClipY, kSheepClipW, kSheepClipH },
        { 0, 0, kScreenW, kSttBannerY + kSttBannerH },
    };
    drawSpeakingParticles(now, kSheepCx, kSheepCy, kSpeakRingRadiusX, kSpeakRingRadiusY,
                          kColBg, sheepExcl, particleExclCount(sheepExcl));
}
static void sheepOnTap(uint32_t now, int /*touchX*/) {
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
static void artOnTap(uint32_t /*now*/, int /*touchX*/) {
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
// 次のタップで喋る「次の宝石」の解説文を先読みしておく（#101）。巡回は決定的（next_scene）なので、
// タップ前にこの WAV を用意でき、タップ時は合成を待たず即再生になる。
static void gemPrefetchNext() {
    const Gem* g = gem_at(next_scene(g_gemIdx, gem_count()));
    if (g) prefetchTts(gem_commentary(*g));
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
    gemPrefetchNext();  // 最初のタップで喋る「次の宝石」の解説を先読みしておく（#101）
}
static void gemUpdate(uint32_t now) {
    // 宝石を少し回してから、専用スプライトに描いてカード本体の上へ push する。
    g_gemAngY += kGemSpinY;
    g_gemAngX += kGemSpinX;
    const Gem* g = gem_at(g_gemIdx);
    if (!g) return;
    g_gem3dCanvas.fillScreen(kColBg);  // カード背景と同色でクリア（push 後に枠が出ないよう）
    drawGem3d(g_gem3dCanvas, gem3d_octahedron(), g_gemAngX, g_gemAngY, 0.0f, g->color);
    g_gem3dCanvas.pushSprite(&M5.Display, kGemCx - kGemHalf, kGemCy - kGemHalf);

    // 解説の読み上げ中、宝石を囲むハローとして楕円パーティクルを背景へ出す（#117）。宝石本体
    // スプライト矩形と上部ヘッダ帯を除外（カード下部は文字が密なので中心を宝石に置き上半分で光らせる）。
    // 背景は宝石カードと同じ kColBg。ジオメトリは実機で調整する初期値。
    constexpr int kGemRingRx     = 104;  // 横半径：宝石スプライト半辺58を越えて左右に出す
    constexpr int kGemRingRy     = 40;   // 縦半径：脈動込みでも名前上端(124)を越えない上限。
                                         // 下端上界 = kGemCy(62)+ry(40)+脈動11+粒半径4 = 117 < 124（名前上端）。
                                         // 上下は本体スプライトに隠れ左右ハローが残るが、旧52比で縦は約2割平たくなる
                                         // （X到達=rx104依存で不変・halo消失なし。平たさは実機#111で最終確認）（#119・ポケ#117と同型）
    constexpr int kGemHeaderH    = 22;   // 上部ヘッダ（図鑑名・通し番号）の帯の高さ
    const ParticleExcl gemExcl[] = {
        { kGemCx - kGemHalf, kGemCy - kGemHalf, kGemHalf * 2, kGemHalf * 2 },  // 宝石スプライト
        { 0, 0, kScreenW, kGemHeaderH },                                      // 上部ヘッダ帯
    };
    drawSpeakingParticles(now, kGemCx, kGemCy, kGemRingRx, kGemRingRy,
                          kColBg, gemExcl, particleExclCount(gemExcl));
}
static void gemOnTap(uint32_t /*now*/, int /*touchX*/) {
    // 次の宝石へ。巡回は実装済みの next_scene を流用する（新規ロジックを作らない）。
    resetSpeakingParticles();  // カードを描き直すので前の宝石の残り粒を捨てる（#117）
    g_gemIdx = next_scene(g_gemIdx, gem_count());
    gemDrawCard();  // 名前・明細・通し番号を更新（宝石本体は直後の update が描く）

    // 送った先の宝石の解説を、ずんだもん声で読み上げる（P5 M2a / Issue #94）。
    // 文字列組み立ては native テスト済みの純粋層 gem_commentary に委譲し、鳴らすだけを実機で行う。
    // 失敗（オフライン等）なら黙って図鑑閲覧を続行する。
    const Gem* g = gem_at(g_gemIdx);
    if (g) speakTts(gem_commentary(*g));  // gemEnter/前タップで先読み済み → 合成待ちなく即再生（#101）

    gemPrefetchNext();  // 次のタップに備えて、さらに次の宝石の解説を先読みしておく（#101）
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
// jiggle 用の帯（スプライト矩形を左右の最大振幅ぶん広げた領域）。ここだけ毎フレーム更新する。
// 左右に kShakeAmplitudePx(=±10) 揺れても収まる幅にし、この帯を1回で push してちらつきを消す（#82）。
constexpr int kPokeBandX = kPokeSprX - kShakeAmplitudePx;      // 帯の左端X（112-10=102）
constexpr int kPokeBandY = kPokeSprY;                          // 帯の上端Y（スプライトと同じ 18）
constexpr int kPokeBandW = kPokeSprW + 2 * kShakeAmplitudePx;  // 帯の幅（96+20=116）
constexpr int kPokeBandH = kPokeSprH;                          // 帯の高さ（96）
// 発話中パーティクル（#117）のジオメトリ。宝石ハローと同形：スプライト中心に楕円リングを置き、
// 揺れるスプライトを内包する jiggle 帯と上部ヘッダ帯を除外して、左右のハローだけを背景へ光らせる。
// 中心は上段スプライトの中心。見せ場の左右ロブは y≈cy なので rx だけで決まり、ry は上下の膨らみ
// （元々ヘッダ帯・jiggle 帯に隠れる部分）を決める。ry は「大音量ピーク時でも名前を食わない」上限で
// 決める：純粋層 particle_ring は基準半径に脈動 pulse(最大11)＋粒半径(最大4)を足すので、下端の
// 到達は cy+ry+15。名前(kGemNameY=136・中心)の上端≈124 の手前に収めるため ry=40（66+40+15=121<124）。
constexpr int kPokeRingCx  = kPokeSprX + kPokeSprW / 2;  // スプライト中心X（160）
constexpr int kPokeRingCy  = kPokeSprY + kPokeSprH / 2;  // スプライト中心Y（66）
constexpr int kPokeRingRx  = 104;                        // 横半径：帯を越えて左右にハローを出す
constexpr int kPokeRingRy  = 40;                         // 縦半径：脈動込みでも名前上端(124)を越えない上限
constexpr int kPokeHeaderH = 22;                         // 上部ヘッダ（図鑑名・通し番号）の帯高
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
// jiggle 合成用の帯スプライト（内蔵RAM・約22KB）。スプライト帯だけをオフスクリーンで合成し
// 画面へは1回だけ push する＝毎フレーム全画面 push で出ていたちらつきを消すための二重バッファ。
static M5Canvas g_pokeBand(&M5.Display);
// 受信スプライト（RGB565 raw 18432B）。再取得のたびに再利用する。PSRAM に確保。
static uint8_t* g_pokeSprite    = nullptr;
int             g_pokeId        = 1;       // 現在表示中の図鑑番号（1..kPokeMaxId で循環）
Pokemon         g_poke;                    // 直近取得した情報（描画に使う）
bool            g_pokeHasSprite = false;   // スプライト取得に成功したか（失敗時は画像を出さない）

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

// 鳴き声の音量倍率（#99）。VOICEROID の読み上げ(TTS)に対して鳴き声が大きすぎたので控えめにする。
// 音量レベル自体は変えず、この再生だけ scale を掛ける（数値は耳で微調整可）。
constexpr float kCryVolumeScale = 0.5f;

// 中継 /pokemon/cry/{id} から WAV を取得して鳴らす（実機依存部・#81）。
// speakTts の写経だが、POST body ではなく id 指定の GET。再生末尾は playWavBuffer を共有する。
// 失敗しても黙って鳴らさず図鑑閲覧を止めない（オフライン/上流失敗でもカードは見られる）。
static bool speakCry(int id) {
    if (!g_voiceEnabled) return false;  // 音声 OFF：鳴き声 HTTP を張らない（#132）
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

    // WAV ヘッダを剥がして PCM を再生（TTS と共有の共通末尾）。鳴き声は控えめの音量で（#99）。
    return playWavBuffer(g_cryBuf, got, kCryVolumeScale);
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

// jiggle の1フレームを描く（#82 ちらつき対策）。旧実装は毎フレーム「全画面 push→スプライト push」
// を画面へ直接行い、その隙間でスプライト位置がカード背景だけになって点滅していた。ここでは
// スプライト帯だけをオフスクリーン(g_pokeBand)で合成し、画面へは1回だけ push する＝1画素あたりの
// 画面書き込みが1回になり点滅しない。転送量も 320x240(150KB)→帯(≈22KB)に減って速い。
static void pokeJiggleFrame(int dx) {
    if (!g_pokeHasSprite || !g_pokeSprite) return;
    if (!g_pokeBand.getBuffer()) g_pokeBand.createSprite(kPokeBandW, kPokeBandH);  // 初回のみ内蔵RAMへ確保
    // 1) カードの帯領域を帯スプライトへ複写（負オフセットで帯位置を原点に合わせ、帯境界でクリップ）。
    //    これでスプライトの旧位置がカード背景で塗り消され、残像(trail)が残らない。
    g_pokeCanvas.pushSprite(&g_pokeBand, -kPokeBandX, -kPokeBandY);
    // 2) 受信スプライト(LE RGB565)を帯へ dx ずらして重ねる。帯内ローカルX = 振幅ぶんの余白 + dx。
    //    直接 Display へ出す pokePushSprite と同じく、送り込み時に setSwapBytes(true) で上下バイトを揃える(#80)。
    g_pokeBand.setSwapBytes(true);
    g_pokeBand.pushImage(kShakeAmplitudePx + dx, 0, kPokeSprW, kPokeSprH,
                         reinterpret_cast<const uint16_t*>(g_pokeSprite), kPokeTransKey);
    g_pokeBand.setSwapBytes(false);
    // 3) 合成済みの帯を画面へ1回で push（画面更新はこの1回だけ＝ちらつかない）。
    g_pokeBand.pushSprite(&M5.Display, kPokeBandX, kPokeBandY);
}

// 発話中パーティクルを1フレーム描く（#117 共通化）。鳴き声(cry)の揺れループと解説(commentary)の
// pokeUpdate 双方から同一ジオメトリで呼ぶための共有ヘルパー。統合層 drawSpeakingParticles は
// 実際に鳴っていなければ早期 return する（＝静止画表示中はほぼ無コスト）。除外＝揺れるスプライトを
// 内包する jiggle 帯 と 上部ヘッダ帯。背景は宝石カードと同じ kColBg。
static void drawPokeSpeakingParticles(uint32_t now) {
    const ParticleExcl pokeExcl[] = {
        { kPokeBandX, kPokeBandY, kPokeBandW, kPokeBandH },  // 揺れるスプライトを内包する帯
        { 0, 0, kScreenW, kPokeHeaderH },                    // 上部ヘッダ帯
    };
    drawSpeakingParticles(now, kPokeRingCx, kPokeRingCy, kPokeRingRx, kPokeRingRy,
                          kColBg, pokeExcl, particleExclCount(pokeExcl));
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
static void pokeUpdate(uint32_t now) {
    // ポケ本体は静止画（取得済みの1枚を出しっぱなし）。タップ時の揺れ(jiggle)は onTap 内で鳴き声と
    // 同時に完結させるので、本体の再描画はしない（#98）。ただし解説(commentary)の読み上げは
    // speakTts が非同期再生で即 return し、以後この update が毎フレーム呼ばれるので、ここで発話中
    // パーティクルを描く（#117・②解説フェーズ）。鳴き声＋揺れの①フェーズは onTap のループ内で描く。
    drawPokeSpeakingParticles(now);
}
static void pokeOnTap(uint32_t /*now*/, int /*touchX*/) {
    resetSpeakingParticles();  // カードを描き直すので前の図鑑ページの残り粒を捨てる（#117・宝石と同じ）
    g_pokeId = (g_pokeId % kPokeMaxId) + 1;  // 1..kPokeMaxId を循環（151 の次は 1）
    pokeLoad();

    // 1) 送った先のポケモンの鳴き声を鳴らす（#81・#99 で読み上げより控えめの音量）。playRaw は非同期。
    speakCry(g_pokeId);

    // 2) 鳴き声の再生中、その場でポケモンを揺らす（#98）。以前は「鳴き声待ち＋TTS取得」を全部
    //    ブロッキングで終えた後に update 経由で揺らしていたため、震えが大きく遅れていた。ここで直接
    //    描くことでタップ直後（＝鳴き声と同時）に震える。待ちループを idle(delay) から「毎フレーム
    //    揺れを描く」へ置き換えただけで、鳴き声が終わるまで待つブロッキング時間は従来と同じ。
    const uint32_t shakeStart = millis();
    const uint32_t deadline   = shakeStart + 4000;  // NW 異常等での無限待ち防止（従来の cry 待ちと同じ上限）
    for (;;) {
        const uint32_t elapsed = millis() - shakeStart;
        const bool shaking = elapsed < kShakeDurationMs;  // 揺れがまだ減衰しきっていない
        const bool crying  = M5.Speaker.isPlaying();      // 鳴き声がまだ鳴っている
        if ((!shaking && !crying) || static_cast<int32_t>(millis() - deadline) >= 0) break;
        // 揺れている間だけ帯を描く（sheep_shake_offset は純粋関数・test_sheep で検証済み）。
        if (shaking && g_pokeHasSprite) pokeJiggleFrame(sheep_shake_offset(elapsed));
        // 鳴き声(cry)に連動した発話中パーティクルを毎フレーム描く（#117・①鳴き声フェーズ）。
        // 揺れが減衰しても鳴き声が続く間は crying で回り続けるので、その間もハローが出る。
        // 除外帯にスプライトを内包しているので、リング粒子と jiggle 描画は干渉しない。
        drawPokeSpeakingParticles(millis());
        delay(16);  // ≈60fps
    }
    // 揺れ終わりを中心（dx=0）で確定させ、静止画へ戻す（帯の最終フレームを中央に揃える）。
    if (g_pokeHasSprite) pokeJiggleFrame(0);

    // 3) 鳴き声に続けて、そのポケモンの解説をずんだもん声で読み上げる（P5 M2a / Issue #94）。
    //    speakTts は先頭で Speaker.stop() を呼ぶので、揺れ＝鳴き声が終わってから読み上げに移る。
    //    失敗（オフライン等）なら黙って図鑑閲覧を続行する。
    speakTts(pokemon_commentary(g_poke));
}

// ───────── 音量シーン（テーマ 音量 / Issue #70：左右タップで増減・音量バー表示） ─────────
// 表示は静的（タップ変更時だけ描く。羊/宝石カードと同じく毎フレームは描かない）。
// 音量状態 g_volumeLevel は純粋ロジック volume.* で増減し、実機へは applyVolume で反映する。

// 音量バーのレイアウト（rotation(1) 320x240 の中央付近）。
constexpr int kVolBarX = 40;
constexpr int kVolBarY = 108;
constexpr int kVolBarW = 240;
constexpr int kVolBarH = 26;
constexpr uint16_t kColVolOn  = TFT_GREEN;  // 点灯セル
constexpr uint16_t kColVolOff = 0x4208;     // 消灯セル（暗いグレー）

// 音量バー＋レベル数値＋左右の操作ヒントを1画面ぶん描く（enter とタップ変更時に呼ぶ）。
static void drawVolumeScreen() {
    M5.Display.fillScreen(kColBg);

    // タイトル（和文・中央上）。
    M5.Display.setFont(&fonts::lgfxJapanGothic_24);
    M5.Display.setTextColor(TFT_WHITE, kColBg);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("音量", kScreenW / 2, 44);

    // セグメントバー（kVolumeMax 個のセル。現在レベルぶんだけ点灯）。
    const int cells = kVolumeMax;
    const int gap   = 4;
    const int cellW = (kVolBarW - gap * (cells - 1)) / cells;
    for (int i = 0; i < cells; ++i) {
        const int x = kVolBarX + i * (cellW + gap);
        const uint16_t col = (i < g_volumeLevel) ? kColVolOn : kColVolOff;
        M5.Display.fillRect(x, kVolBarY, cellW, kVolBarH, col);
    }

    // 数値ラベル（現在/最大）。
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_CYAN, kColBg);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", g_volumeLevel, kVolumeMax);
    M5.Display.drawString(buf, kScreenW / 2, kVolBarY + kVolBarH + 26);
    M5.Display.setTextSize(1);

    // 操作ヒント（左=下げ / 右=上げ）。
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextColor(TFT_WHITE, kColBg);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.drawString("<< 下げる", 10, kScreenH - 22);
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.drawString("上げる >>", kScreenW - 10, kScreenH - 22);

    // 後続描画に影響しないよう既定へ戻す。
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextDatum(textdatum_t::top_left);
}

static void volumeEnter() {
    drawVolumeScreen();  // 入場時に一度だけ描く（以後はタップ時だけ更新）
}
static void volumeUpdate(uint32_t /*now*/) {
    // 静的表示なので毎フレームは描かない（無駄な全画面再描画を避ける）。
}
static void volumeOnTap(uint32_t /*now*/, int touchX) {
    // 右半分タップ=上げ / 左半分=下げ（判定は純粋ロジック volume_is_up_tap）。
    g_volumeLevel = volume_is_up_tap(touchX, kScreenW) ? volume_up(g_volumeLevel)
                                                       : volume_down(g_volumeLevel);
    // 変更を耳で確認できるよう、新しい音量で試聴のメェを鳴らす（applyVolume は playBleat 内で実施）。
    playBleat();
    drawVolumeScreen();  // バー・数値を更新
}

// ───────── 話者シーン（Issue #105：左右タップで voiceroid を切替・選択名表示・試聴） ─────────
// 音量シーンと同形：静的表示（タップ変更時だけ描く）、状態 g_voiceIdx は純粋ロジック voice_select.* で巡回。
static void drawVoiceScreen() {
    M5.Display.fillScreen(kColBg);

    // タイトル（和文・中央上）。
    M5.Display.setFont(&fonts::lgfxJapanGothic_24);
    M5.Display.setTextColor(TFT_WHITE, kColBg);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("話者", kScreenW / 2, 44);

    // 現在の話者名（大きく中央）。
    M5.Display.setFont(&fonts::lgfxJapanGothic_36);
    M5.Display.setTextColor(TFT_CYAN, kColBg);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString(voice_name_at(g_voiceIdx), kScreenW / 2, kScreenH / 2);

    // 位置（現在/総数）。
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, kColBg);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", g_voiceIdx + 1, voice_option_count());
    M5.Display.drawString(buf, kScreenW / 2, kScreenH / 2 + 44);
    M5.Display.setTextSize(1);

    // 操作ヒント（左=前 / 右=次）。
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextColor(TFT_WHITE, kColBg);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.drawString("<< 前の声", 10, kScreenH - 22);
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.drawString("次の声 >>", kScreenW - 10, kScreenH - 22);

    // 後続描画に影響しないよう既定へ戻す。
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextDatum(textdatum_t::top_left);
}

static void voiceEnter() {
    drawVoiceScreen();  // 入場時に一度だけ描く（以後はタップ時だけ更新）
}
static void voiceUpdate(uint32_t /*now*/) {
    // 静的表示なので毎フレームは描かない（音量シーンと同じ）。
}
static void voiceOnTap(uint32_t /*now*/, int touchX) {
    // 右半分=次の声 / 左半分=前の声（判定は純粋ロジック voice_is_next_tap）。
    g_voiceIdx = voice_is_next_tap(touchX, kScreenW) ? voice_next(g_voiceIdx)
                                                     : voice_prev(g_voiceIdx);
    drawVoiceScreen();  // 選択名・位置を更新
    // 選んだ声で試聴：その話者が自分の名前を名乗る（オフライン等で失敗しても黙って続行）。
    speakTts(std::string(voice_name_at(g_voiceIdx)) + "です。");
}

// --- スタックちゃんシーン（本家 M5Stack-Avatar・#125 M1） ---
// 他シーンと違い、描画は loop() ではなくライブラリが立てる2本の FreeRTOS タスクが行う。
//   drawLoop   … M5.Display へ顔を描き続ける
//   facialLoop … まばたき・サッケード（視線の揺れ）・呼吸の内部状態を進める
// よって update() は何もせず、enter/exit でタスクの生死だけを管理する。
static m5avatar::Avatar g_stackchan;

static void stackchanEnter() {
    // 背景も含めてライブラリが全面を描くので、こちらで塗る必要はない。
    // 前回の exit で2タスクの消滅を確認済みなので、ここでの start() が二重起動になることはない。
    g_stackchan.start();
}
static void stackchanUpdate(uint32_t /*now*/) {
    // 描画は drawLoop タスクの担当。ここで M5.Display に触ると奪い合いになるので何もしない。
}
static void stackchanOnTap(uint32_t /*now*/, int /*touchX*/) {
    // 発話と口パクは #125 M2 で配線する。
}
// stop() を要求したアバターの2タスクが自己削除し終わるまで待つ。
// 名前で引いて NULL が返る＝そのタスクはもう存在しない。
//
// なぜ固定 delay ではないか：Avatar::stop() は _isDrawing フラグを下ろすだけで、進行中の
// Face::draw() を中断できない。その draw() は画面を高さ8pxの短冊30枚に分けて DMA 転送し、
// 毎回 lgfx::delay(1) を挟む（Face.cpp）。つまり stop() の直後に draw() へ入られると、
// 最低でも 30ms、実際にはスプライト生成と30回の pushRotateZoom を足して数十ms 走り切ってから
// ようやくフラグを見に来る。固定の待ち時間はこの所要時間の見積もりに賭けることになる。
//
// なぜハンドルを保持して eTaskGetState しないか：両タスクは vTaskDelete(NULL) で自己削除し、
// TCB は idle タスクが解放する。解放後のハンドルを覗くのは未定義動作になる。
// 名前で引く xTaskGetHandle はハンドルを保持しないので、その危険が無い。
//
// タイムアウトは保険。将来ライブラリがタスク名を変えたら NULL が返らずここで空回りするが、
// 上限で必ず抜けて従来どおり「十分待った」状態へ縮退する（表示が乱れうるだけで停止はしない）。
constexpr uint32_t kAvatarTeardownTimeoutMs = 400;

// ライブラリが xTaskCreateUniversal に渡すタスク名（Avatar.cpp）。
// drawLoop   … M5.Display へ DMA 転送する。fillScreen と競合するので必ず消滅を待つ。
// facialLoop … 内部の float を書くだけで Display には触れない。表示は競合しないが、
//              生き残ったまま再 start() すると2本目が走り、呼吸・視線の状態を奪い合う。
constexpr const char* kAvatarTaskNames[] = { "drawLoop", "facialLoop" };

static void waitAvatarTasksGone() {
    const uint32_t deadline = millis() + kAvatarTeardownTimeoutMs;
    // millis() のラップアラウンドに耐えるよう、差の符号で判定する（絶対値の大小比較にしない）。
    while (static_cast<int32_t>(millis() - deadline) < 0) {
        bool alive = false;
        for (const char* name : kAvatarTaskNames) {
            if (xTaskGetHandle(name) != nullptr) { alive = true; break; }
        }
        if (!alive) return;  // 両方の自己削除を確認できた
        // 他タスクへ CPU を譲る（最後の draw() を終えるのを待つ）。delay は vTaskDelay に落ちるので
        // loopTask 自体がブロックし、同優先度(1)の drawLoop も上位(2)の facialLoop も確実に走る。
        // 5ms が 5tick になるのは Arduino-ESP32 が tick を 1000Hz に設定しているため（素の ESP-IDF 既定
        // 100Hz では 0tick = 単なる yield に落ちる）。移植時はここが前提になる。
        delay(5);
    }
    // ここに来た＝上限まで待っても名前が引け続けた。実際にはタスクは _isDrawing=false で
    // 自滅しているはず（drawLoop は draw 最悪~70ms + TaskDelay 10ms、facialLoop は ~33ms）なので
    // 表示が壊れる可能性は低いが、ライブラリのタスク名が変わった徴候なので必ず気付けるようにする。
    Serial.println("[avatar] teardown wait timed out; task name may have changed upstream");
}


static void stackchanExit() {
    // 停止を要求 → 2タスクが消えたことを確認 → はじめて自分で塗る。
    // この順を守らないと、生きている drawLoop の DMA 転送と fillScreen が同一バスへ並行アクセスし、
    // 表示が化けたり顔が残ったりする。M5GFX のバストランザクションはタスク安全ではない。
    //
    // ここで消滅を確認しておくことが、再訪時の二重起動も同時に防ぐ。start() は
    // `if (_isDrawing) return;` でしか守られておらず、stop() 直後（フラグ false・タスクは生存）に
    // start() すると、古いタスクが停止フラグを見る前に true へ戻り、古い方も終了しそこねて
    // 2本が並走してしまうためである。
    g_stackchan.stop();
    waitAvatarTasksGone();
    M5.Display.fillScreen(kColBg);  // 次シーンへ渡す前に顔を消しておく
}

// ───────── パックマン シーン（#134 Step2：迷路描画＋タッチ十字パッド） ─────────
// ゲームロジック（迷路・移動可否・1マス移動）は純粋ロジック層 pacman.{h,cpp}（native テスト済み）に
// 委譲し、ここは「実機描画」と「タッチ入力」だけを担う。gem3d と同じ責務分離。
//
// 操作モデル: 本アプリ共通の「長押し＝メニューに戻る」と衝突しないよう、押しっぱなしではなく
// アーケード版と同じ「十字パッドをタップして進行方向をセット→あとは自動で進み続ける」方式にする。
// 曲がりたい所でその方向を短くタップする、本家そのままの操作感。
constexpr int kPacTile = 16;                       // 1マスの画素サイズ（15*16=240, 13*16=208 に収まる）
constexpr int kPacOx   = 0;                        // 迷路の描画原点X
constexpr int kPacOy   = 0;                        // 迷路の描画原点Y
constexpr uint32_t kPacStepMs = 180;               // 1マス進む間隔（ミリ秒）＝移動速度
constexpr uint16_t kColPacWall   = 0x18FF;         // 迷路の壁（明るい青）
constexpr uint16_t kColPacDot    = TFT_WHITE;      // ドット
constexpr uint16_t kColPacPower   = TFT_ORANGE;    // パワーエサ
constexpr uint16_t kColPacPlayer = TFT_YELLOW;     // プレイヤー
constexpr uint16_t kColPacGhost  = TFT_RED;        // ゴースト（通常時・Step4）
constexpr uint16_t kColPacFright = 0x781F;         // 逃走中のゴースト（青紫・Step5b。壁の水色と区別）
constexpr uint16_t kColPacPad    = 0x4208;         // 十字パッドのボタン（暗いグレー）
constexpr uint16_t kColPacPadIcon = TFT_WHITE;     // 十字パッドの矢印

// 十字パッドの当たり矩形（画面右の 80px 帯：x∈[240,320]）。順番は Up/Down/Left/Right。
struct PacRect { int x0, y0, x1, y1; };
constexpr PacRect kPadUp    { 268,  78, 294, 104 };
constexpr PacRect kPadDown  { 268, 138, 294, 164 };
constexpr PacRect kPadLeft  { 242, 108, 268, 134 };
constexpr PacRect kPadRight { 294, 108, 320, 134 };

// ゲーム状態。プレイヤー位置・進行方向・スコア・回収済みマスは純粋層 PacGame（native テスト済み）が持つ。
// このシーン局所の一時変数（予約方向・タイマ・タッチ立ち上がり）だけ描画層で保持する。
PacGame  g_pacGame;                 // 迷路プレイの状態（pac_game_init / pac_game_advance で更新）
Dir      g_pacDesired  = Dir::None; // タップで予約した進行方向（曲がれる所で採用）
uint32_t g_pacLastStep = 0;         // 最後に1マス進んだ時刻
bool     g_pacPrevTouch = false;    // 前フレームで触れていたか（押した瞬間だけ拾う立ち上がり検出）

// 点 (x,y) がどの十字パッドボタンの上か。どのボタンでもなければ Dir::None。
static bool pacInRect(const PacRect& r, int x, int y) {
    return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
}
static Dir pacDpadDirAt(int x, int y) {
    if (pacInRect(kPadUp, x, y))    return Dir::Up;
    if (pacInRect(kPadDown, x, y))  return Dir::Down;
    if (pacInRect(kPadLeft, x, y))  return Dir::Left;
    if (pacInRect(kPadRight, x, y)) return Dir::Right;
    return Dir::None;
}

// 1マス (tx,ty) を現在の見た目通りに描く（プレイヤーの通過跡を消す時にも使う）。
// pac_current_tile を使うので、回収済みのドット／パワーエサは床として描かれ二度と復活しない。
static void pacDrawTile(int tx, int ty) {
    const int px = kPacOx + tx * kPacTile;
    const int py = kPacOy + ty * kPacTile;
    M5.Display.fillRect(px, py, kPacTile, kPacTile, kColBg);  // まず背景で消す
    const int cx = px + kPacTile / 2;
    const int cy = py + kPacTile / 2;
    switch (pac_current_tile(g_pacGame, tx, ty)) {
        case Tile::Wall:
            // 1px 内側に塗って升目の境界を見せる（隣の壁と繋がって見えるのを防ぐ）
            M5.Display.fillRect(px + 1, py + 1, kPacTile - 2, kPacTile - 2, kColPacWall);
            break;
        case Tile::Dot:
            M5.Display.fillRect(cx - 1, cy - 1, 2, 2, kColPacDot);
            break;
        case Tile::Power:
            M5.Display.fillCircle(cx, cy, 4, kColPacPower);
            break;
        case Tile::Empty:
        default:
            break;  // 何も描かない（背景のまま）
    }
}

// 迷路全体を1回描く（シーン入場時）。
// 迷路の大きさは純粋層の実行時値（pac_maze_w/h）なので static_assert では守れない。
// Step3 以降で kMaze を広げた時に十字パッド帯（x≥kPadLeft.x0）や画面外へ静かにはみ出さないよう、
// 描画可能なタイル範囲にクランプする安全網を置く（はみ出す分は描かない＝壊れず縮退する）。
static void pacDrawMaze() {
    const int maxTx = kPadLeft.x0 / kPacTile;  // 十字パッド帯より左に収まるタイル数
    const int maxTy = kScreenH / kPacTile;     // 画面高さに収まるタイル数
    const int drawW = pac_maze_w() < maxTx ? pac_maze_w() : maxTx;
    const int drawH = pac_maze_h() < maxTy ? pac_maze_h() : maxTy;
    for (int ty = 0; ty < drawH; ++ty)
        for (int tx = 0; tx < drawW; ++tx)
            pacDrawTile(tx, ty);
}

// プレイヤーを (tx,ty) の升目中心に黄色い丸で描く。
static void pacDrawPlayer(int tx, int ty) {
    const int cx = kPacOx + tx * kPacTile + kPacTile / 2;
    const int cy = kPacOy + ty * kPacTile + kPacTile / 2;
    M5.Display.fillCircle(cx, cy, kPacTile / 2 - 2, kColPacPlayer);
}

// ゴーストを (tx,ty) の升目中心に丸で描く。色は通常＝赤／逃走中＝青紫（Step5b）。
static void pacDrawGhost(int tx, int ty, uint16_t color) {
    const int cx = kPacOx + tx * kPacTile + kPacTile / 2;
    const int cy = kPacOy + ty * kPacTile + kPacTile / 2;
    M5.Display.fillCircle(cx, cy, kPacTile / 2 - 2, color);
}

// 十字パッドを1回描く（シーン入場時）。各ボタンに三角形の矢印を載せる。
static void pacDrawDpad() {
    const PacRect btns[4] = {kPadUp, kPadDown, kPadLeft, kPadRight};
    for (const PacRect& r : btns) {
        M5.Display.fillRoundRect(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0, 4, kColPacPad);
    }
    const int m = 6;  // 矢印の大きさ
    // Up
    { const int cx = (kPadUp.x0 + kPadUp.x1) / 2, cy = (kPadUp.y0 + kPadUp.y1) / 2;
      M5.Display.fillTriangle(cx, cy - m, cx - m, cy + m, cx + m, cy + m, kColPacPadIcon); }
    // Down
    { const int cx = (kPadDown.x0 + kPadDown.x1) / 2, cy = (kPadDown.y0 + kPadDown.y1) / 2;
      M5.Display.fillTriangle(cx, cy + m, cx - m, cy - m, cx + m, cy - m, kColPacPadIcon); }
    // Left
    { const int cx = (kPadLeft.x0 + kPadLeft.x1) / 2, cy = (kPadLeft.y0 + kPadLeft.y1) / 2;
      M5.Display.fillTriangle(cx - m, cy, cx + m, cy - m, cx + m, cy + m, kColPacPadIcon); }
    // Right
    { const int cx = (kPadRight.x0 + kPadRight.x1) / 2, cy = (kPadRight.y0 + kPadRight.y1) / 2;
      M5.Display.fillTriangle(cx + m, cy, cx - m, cy - m, cx - m, cy + m, kColPacPadIcon); }
}

// スコアを画面右上（十字パッド帯の上・y<48）に描く。入場時とスコア変化時だけ呼ぶ。
static void pacDrawScore() {
    const int x = kPadLeft.x0;                       // 迷路(x<240)より右の帯に置く
    M5.Display.fillRect(x, 8, kScreenW - x, 40, kColBg);  // 数字が伸びても消えるよう帯ごと消去
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextColor(TFT_WHITE, kColBg);
    M5.Display.setCursor(x + 2, 10);
    M5.Display.print("SCORE");
    M5.Display.setCursor(x + 2, 28);
    M5.Display.printf("%d", g_pacGame.score);
}

// 全ゴーストを現在位置に描く。逃走中（fright_timer>0）は青紫で「食べられる」ことを示す。
static void pacDrawGhosts() {
    const uint16_t color = (g_pacGame.fright_timer > 0) ? kColPacFright : kColPacGhost;
    for (int i = 0; i < kPacGhostCount; ++i)
        pacDrawGhost(g_pacGame.ghosts[i].pos.x, g_pacGame.ghosts[i].pos.y, color);
}

// 決着（Dead/Clear）の帯を迷路の中央に重ねて描く。局面で文言と枠色を変える。
static void pacDrawResult(PacPhase phase) {
    const bool clear = (phase == PacPhase::Clear);
    const uint16_t accent = clear ? TFT_GREEN : TFT_RED;
    const char* title = clear ? "CLEAR!" : "GAME OVER";
    const int w = 180, h = 46;
    const int x = (240 - w) / 2;               // 迷路描画域（x<240）の中央
    const int y = (kScreenH - h) / 2;
    M5.Display.fillRoundRect(x, y, w, h, 6, TFT_BLACK);
    M5.Display.drawRoundRect(x, y, w, h, 6, accent);
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(accent, TFT_BLACK);
    M5.Display.drawString(title, x + w / 2, y + h / 2 - 8);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("長押しでメニューへ", x + w / 2, y + h / 2 + 10);
    M5.Display.setTextDatum(textdatum_t::top_left);  // 既定に戻す（他描画への影響を避ける）
}

static void pacEnter() {
    M5.Display.fillScreen(kColBg);
    g_pacGame     = pac_game_init();  // プレイヤー/ゴースト初期位置・スコア0・全ペレット未回収・Playing
    g_pacDesired  = Dir::None;
    g_pacLastStep = millis();
    // 入場は長押し（メニュー決定）で来るため指がまだ触れている可能性がある。
    // 立ち上がり検出の誤発火を防ぐため「触れている」状態から始める（一度離すまで無反応）。
    g_pacPrevTouch = true;
    pacDrawMaze();
    pacDrawDpad();
    pacDrawScore();
    pacDrawPlayer(g_pacGame.player.x, g_pacGame.player.y);
    pacDrawGhosts();
}

static void pacUpdate(uint32_t now) {
    // 決着後は入力も進行も止める（長押しでメニューへ戻り、再入場でリスタート）。
    if (g_pacGame.phase != PacPhase::Playing) return;

    // ① 入力：押した瞬間（立ち上がり）だけ、その座標が十字パッドのどのボタンかを見て進行方向を予約。
    const bool touching = (M5.Touch.getCount() > 0);
    if (touching && !g_pacPrevTouch) {
        const auto d = M5.Touch.getDetail();
        const Dir cmd = pacDpadDirAt(d.x, d.y);
        if (cmd != Dir::None) g_pacDesired = cmd;
    }
    g_pacPrevTouch = touching;

    // ② 進行：一定間隔ごとに1ティック。プレイヤー移動・ゴースト追跡・衝突判定は純粋層に委譲する。
    if (now - g_pacLastStep < kPacStepMs) return;
    g_pacLastStep = now;

    const int prevScore = g_pacGame.score;
    const PacTickResult r = pac_game_tick(g_pacGame, g_pacDesired);

    // 動く前の位置を現在の見た目に戻してから、新しい位置へプレイヤーと全ゴーストを描く。
    // ゴーストの通過跡（ドット等）も pacDrawTile が正しく復元する。
    pacDrawTile(r.player_from.x, r.player_from.y);
    for (int i = 0; i < kPacGhostCount; ++i)
        pacDrawTile(r.ghost_from[i].x, r.ghost_from[i].y);
    pacDrawPlayer(g_pacGame.player.x, g_pacGame.player.y);
    pacDrawGhosts();
    if (g_pacGame.score != prevScore) pacDrawScore();  // 得点した時だけスコア再描画

    // 決着した瞬間に結果表示（Dead=GAME OVER / Clear=CLEAR!）。
    if (g_pacGame.phase != PacPhase::Playing) pacDrawResult(g_pacGame.phase);
}

static void pacOnTap(uint32_t /*now*/, int /*touchX*/) {
    // 進行方向の指定は pacUpdate の立ち上がり検出で処理する（Tap は離した後の確定イベントで
    // 座標が Y を含まないため、十字パッド判定には向かない）。ここは何もしない。
}

// ───────── 動画再生シーン（Issue #142：骨組み。実再生は次ステップで SD フレーム化） ─────────
// いまは「準備中」プレースホルダ。YouTube 生ストリーム（H.264/VP9・DRM）は ESP32-S3 単体では
// 復号・デコードできないため、最終的には「動画→JPEGフレーム列＋WAV」に事前変換して microSD から
// drawJpg＋playRaw で再生する方針。このシーンはまず kScenes[] へ登録し（開放閉鎖の原則）、
// フレーム時刻の純粋ロジック video_frame_at を準備中アニメ（巡回するドット）で実際に動かしておく。
// 次ステップで、このドット数計算がそのまま「表示すべきフレーム番号」計算に置き換わる。
static uint32_t g_videoEnterMs = 0;   // シーンに入った時刻（経過時間の起点）
static int      g_videoDots    = -1;  // 直近に描いたドット数（変化時だけ描き直す＝ちらつき回避）

constexpr int kVideoDotFps    = 2;    // 準備中アニメの速さ（2fps＝0.5秒ごとに1コマ進む）
constexpr int kVideoDotFrames = 4;    // ドットは 0..3 個を巡回

static void videoEnter() {
    M5.Display.fillScreen(kColBg);
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextDatum(textdatum_t::top_left);

    M5.Display.setTextColor(TFT_CYAN, kColBg);
    M5.Display.drawString("動画再生", 8, 12);

    // この段階の位置づけを画面にも明示する（実再生はまだ無い）。
    M5.Display.setTextColor(TFT_WHITE, kColBg);
    M5.Display.drawString("準備中なのだ", 8, 96);
    M5.Display.drawString("SDフレーム再生は次のステップ", 8, 120);
    M5.Display.drawString("長押しでメニューに戻るのだ", 8, 200);

    M5.Display.setFont(&fonts::Font0);          // 既定へ戻す（他描画への影響回避）
    M5.Display.setTextDatum(textdatum_t::top_left);

    g_videoEnterMs = millis();
    g_videoDots    = -1;  // 次の update で必ず1回描く
}

static void videoUpdate(uint32_t now) {
    // 「準備中」の巡回ドット数を、フレーム時刻の純粋ロジックで決める（次ステップの実フレーム
    // 番号計算と同じ video_frame_at。ここで実際に exercise しておく＝骨組みでも死にコードにしない）。
    const int dots = video_frame_at(now - g_videoEnterMs, kVideoDotFps, kVideoDotFrames);
    if (dots == g_videoDots) return;  // 変化なし＝描き直さない（ちらつき回避）
    g_videoDots = dots;

    // 「準備中なのだ」の下に "." を dots 個。固定幅の領域をクリアしてから描く（前フレームを消す）。
    constexpr int kDotsX = 8;
    constexpr int kDotsY = 150;
    M5.Display.fillRect(kDotsX, kDotsY, 80, 18, kColBg);
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextColor(TFT_WHITE, kColBg);
    M5.Display.setTextDatum(textdatum_t::top_left);
    const std::string s(dots, '.');
    M5.Display.drawString(s.c_str(), kDotsX, kDotsY);
    M5.Display.setFont(&fonts::Font0);  // 既定へ戻す
}

static void videoOnTap(uint32_t /*now*/, int /*touchX*/) {
    // 骨組み段階では短タップの反応は無し（実再生でシーク／一時停止に割り当てる予定）。
}

// シーン表（巡回順）。ここに1要素足すだけで新テーマを増やせる。
// exit を持たないシーンは4要素目を省略する（aggregate 初期化で nullptr になる）。
const SceneDef kScenes[] = {
    { "ひつじ",       sheepEnter,  sheepUpdate,  sheepOnTap  },
    { "アート",       artEnter,    artUpdate,    artOnTap    },
    { "宝石図鑑",     gemEnter,    gemUpdate,    gemOnTap    },
    { "ポケモン図鑑", pokeEnter,   pokeUpdate,   pokeOnTap   },
    { "音量調整",     volumeEnter, volumeUpdate, volumeOnTap },  // 左右タップ・#70
    { "声の選択",     voiceEnter,  voiceUpdate,  voiceOnTap  },  // 左右タップ・#105
    { "スタックチャン", stackchanEnter, stackchanUpdate, stackchanOnTap, stackchanExit },  // 本家アバター（#125）
    { "パックマン",   pacEnter,    pacUpdate,    pacOnTap    },  // 自作パックマン（#134 Step2）
    { "動画再生",     videoEnter,  videoUpdate,  videoOnTap  },  // 骨組み（#142。実再生は SD フレーム化で後続）
};
constexpr int kSceneCount = static_cast<int>(sizeof(kScenes) / sizeof(kScenes[0]));
int g_sceneIdx = 0;  // 現在のシーン番号

// ───────── メニュー画面（シーン選択のハブ / Issue #132） ─────────
// 起動直後と、シーン中の長押しで戻ってくる「ハブ」。縦リストで音声トグル＋全シーンを並べる。
// 上半分タップでカーソル上、下半分タップで下、長押しで決定（先頭=音声トグル、以降=各シーン入場）。
// これまでの「長押しでローテーション巡回」を置き換える（相手サーバー不在のシーンへ不意に入って
// 固まるのを防ぐ）。カーソル移動の純粋ロジックは menu_move（native テスト済み）に委譲する。
bool g_inMenu   = true;   // 起動時はメニュー。シーン中は false。
int  g_menuSel  = 0;      // カーソル位置。0=音声トグル / 1..=各シーン（kScenes の index+1）。
bool g_menuDirty = true;  // 再描画が必要か（選択やトグルが変わった時だけ描く＝ちらつき回避）
constexpr int kMenuItemCount = kSceneCount + 1;  // 先頭の音声トグル + 全シーン

// メニュー行のレイアウト（1行目Y・行間・行の文字高）。static_assert で使うためファイルスコープに置く。
constexpr int kMenuRowY0    = 28;  // 1行目のY
constexpr int kMenuRowH     = 21;  // 行間（シーン増加に合わせ 26→24→21 と詰めた・#134/#142）
constexpr int kMenuRowGlyphH = 16;  // lgfxJapanGothic_16 の文字高（下端計算用）
// 全項目が画面(kScreenH)に収まることをコンパイル時に保証する。シーンを増やして溢れたらここで失敗し、
// 実機で気付く前にビルドで止まる（開放閉鎖：シーン追加でレイアウトが黙って壊れないための安全網・#132）。
static_assert(kMenuRowY0 + (kMenuItemCount - 1) * kMenuRowH + kMenuRowGlyphH <= kScreenH,
              "menu items overflow the screen; reduce kMenuRowH or paginate");

// メニュー1画面を描く。先頭に音声 ON/OFF、続けて各シーン名を縦に並べ、選択行を強調する。
static void menuRender() {
    M5.Display.fillScreen(kColBg);
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextDatum(textdatum_t::top_left);

    // タイトル
    M5.Display.setTextColor(TFT_CYAN, kColBg);
    M5.Display.drawString("メニュー（上下タップ→長押しで決定）", 6, 4);

    for (int i = 0; i < kMenuItemCount; ++i) {
        const int y = kMenuRowY0 + i * kMenuRowH;
        const bool sel = (i == g_menuSel);
        // カーソル（選択行だけ左に三角。フォント非依存で確実に出るよう図形で描く）。
        if (sel) M5.Display.fillTriangle(8, y + 2, 8, y + 14, 16, y + 8, TFT_YELLOW);
        M5.Display.setTextColor(sel ? TFT_YELLOW : TFT_WHITE, kColBg);
        // ラベル（先頭行は音声トグル、以降はシーン名）。
        std::string label = (i == 0)
            ? std::string("音声: ") + (g_voiceEnabled ? "ON" : "OFF")
            : std::string(kScenes[i - 1].name);
        M5.Display.drawString(label.c_str(), 24, y);
    }
    // 既定へ戻す（他描画への影響回避）。
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextDatum(textdatum_t::top_left);
}

// メニューへ入る（起動時／シーン中の長押しから）。1回描いて以後は変化時だけ描き直す。
static void menuEnter() {
    resetSpeakingParticles();  // 直前シーンの残り粒を捨てる（背景を誤って塗り消さない・#117）
    menuRender();
    g_menuDirty = false;
}

// 現在のカーソル行を「決定」する。先頭行なら音声 ON/OFF をトグルしてメニューに留まり、
// それ以外なら対応シーンへ入場する。
static void menuSelect() {
    if (g_menuSel == 0) {
        g_voiceEnabled = !g_voiceEnabled;  // 音声トグル：メニューに留まって表示だけ更新
        g_menuDirty = true;
        return;
    }
    // シーン入場：g_menuDirty はここでは触らない（次にメニューへ戻る時 menuEnter が false に整える。
    // dirty の初期化責務は menuEnter に一本化している）。
    g_sceneIdx = g_menuSel - 1;  // 音声行の1つぶん後ろにシーンが並ぶ
    g_inMenu   = false;
    kScenes[g_sceneIdx].enter();
}

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

    menuEnter();  // 起動時はまずメニュー画面（シーン選択のハブ・#132）
}

void loop() {
    M5.update();
    const uint32_t now = millis();

    // タッチ系列をジェスチャ検出に流し、短タップ/長押しを判定する（純粋ロジック）。
    static TouchTracker tracker;
    const bool touching = (M5.Touch.getCount() > 0);
    // Tap は「離した瞬間」に確定するため、その時点では指が離れて座標が無効になっている。
    // そこで押下中の最後の座標を覚えておき、Tap 確定時に渡す（X=音量/声シーンの左右判定、
    // Y=メニューの上下判定用・#132）。
    static int lastTouchX = kScreenW / 2;
    static int lastTouchY = kScreenH / 2;
    if (touching) {
        const auto d = M5.Touch.getDetail();
        lastTouchX = d.x;
        lastTouchY = d.y;
    }
    const TouchEvent ev = touch_update(tracker, touching, now);

    if (g_inMenu) {
        // ── メニュー画面 ──
        if (ev == TouchEvent::LongPress) {
            menuSelect();  // 音声トグル or シーン入場（入場時 g_inMenu=false になる）
        } else if (ev == TouchEvent::Tap) {
            // 画面の上半分タップ＝カーソル上、下半分＝下（純粋ロジック menu_move でクランプ）。
            const int delta = (lastTouchY < kScreenH / 2) ? -1 : +1;
            const int next  = menu_move(g_menuSel, kMenuItemCount, delta);
            if (next != g_menuSel) { g_menuSel = next; g_menuDirty = true; }
        }
        if (g_menuDirty) { menuRender(); g_menuDirty = false; }  // 変化時だけ描き直す
        delay(33);
        return;
    }

    // ── シーン表示中 ──
    if (ev == TouchEvent::LongPress) {
        // 長押し → メニューへ戻る（ローテーション巡回は廃止・#132）。
        M5.Speaker.stop();         // 前シーンの音声を打ち切る（再生中の切替で発話ハロー誤描画を防ぐ・#119）
        resetSpeakingParticles();  // 前シーンの残り粒を捨てる（#117）
        // 自前の描画タスクを持つシーンに画面を手放させてからメニューを描く（#125）。
        if (kScenes[g_sceneIdx].exit) kScenes[g_sceneIdx].exit();
        g_inMenu = true;
        menuEnter();
        delay(33);
        return;
    } else if (ev == TouchEvent::Tap) {
        // 短タップ → 現シーンの反応に委譲（羊のメェ／アート再生成／音量増減など）。
        kScenes[g_sceneIdx].onTap(now, lastTouchX);
    }

    kScenes[g_sceneIdx].update(now);  // 毎フレーム描画
    delay(33);  // 約30fps
}
