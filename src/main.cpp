#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>   // 中継サーバへの HTTP POST（ESP32 標準）
#include <ArduinoJson.h>  // リクエスト body の安全な組み立て
#include <string>
#include <vector>
#include "avatar.h"
#include "sheep.h"
#include "art.h"      // art_generate（幾何学アートの図形プリミティブ生成・純粋ロジック）
#include "gesture.h"  // touch_update（短タップ/長押し検出・純粋ロジック）
#include "scene.h"    // next_scene（シーン巡回・純粋ロジック）
#include "net.h"
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

// 中継サーバ(/chat)へ一度だけ問い合わせ、結果を g_reply / g_requestedExpr に格納する（実機依存部）。
static void fetchGreeting() {
    HTTPClient http;
    http.begin(RELAY_URL);
    http.addHeader("Content-Type", "application/json");

    // リクエスト body を ArduinoJson で組み立て（特殊文字も安全にエスケープ）。
    JsonDocument req;
    req["message"] = "起動したよ。ひとこと挨拶して。";
    std::string body;
    serializeJson(req, body);

    const int code = http.POST(String(body.c_str()));
    if (code == 200) {
        const String payload = http.getString();
        const ReplyMessage m = parse_relay_reply(payload.c_str());
        g_reply = m.reply;
        g_requestedExpr = m.expression;
    } else {
        // 失敗時も画面で分かるように（sad 表情でエラーコード表示）。
        g_reply = std::string("relay error: ") + std::to_string(code);
        g_requestedExpr = Expression::Sad;
    }
    http.end();
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
constexpr int kSheepClipX = 56;
constexpr int kSheepClipY = 44;
constexpr int kSheepClipW = 208;
constexpr int kSheepClipH = 170;

// 羊の色（RGB565）。
constexpr uint16_t kColWool      = TFT_WHITE;
constexpr uint16_t kColSheepFace = 0xF7BB;  // クリーム色の顔
constexpr uint16_t kColSheepLeg  = 0x4208;  // 暗いグレーの足
constexpr uint16_t kColEar       = 0x8410;  // グレーの耳
constexpr uint16_t kColSheepEye  = TFT_BLACK;

// 羊の目を1つ描く。まばたきは既存のテスト済み eye_openness をそのまま流用する。
static void drawSheepEye(int ex, int ey, float openness) {
    const int halfH = static_cast<int>(7 * openness);
    if (halfH <= 1) {
        M5.Display.fillRect(ex - 4, ey - 1, 8, 2, kColSheepEye);  // ほぼ閉じ＝とじ目
    } else {
        M5.Display.fillEllipse(ex, ey, 4, halfH, kColSheepEye);
    }
}

// 「メェ」をトーン合成で鳴らす（下降2音でそれっぽさを出す。WAVアセット不要）。
static void playBleat() {
    M5.Speaker.setVolume(180);
    M5.Speaker.tone(760, 110);  // メ
    M5.Speaker.tone(560, 190);  // ェ（少し下げる）
}

// 全身ドット羊を1フレーム描く。bob で上下に、shakeX で左右（タップ反応）に揺れ、目はまばたきする。
// 重なり描画なので、固定枠を背景色でクリアしてから毎フレーム全体を描き直す（最小実装）。
static void drawSheep(uint32_t now, int shakeX) {
    const int bob = sheep_bob_offset(now);
    const int cx  = kSheepCx + shakeX;       // タップ反応で左右に揺らす
    const int cy  = kSheepCy + bob;          // bob で全体を上下させる

    M5.Display.fillRect(kSheepClipX, kSheepClipY, kSheepClipW, kSheepClipH, kColBg);

    // 足（胴体の後ろに先に描く）。
    const int legY = cy + 26;
    for (int i = 0; i < 4; ++i) {
        M5.Display.fillRect(cx - 30 + i * 20, legY, 8, 22, kColSheepLeg);
    }

    // モコモコ胴毛：白い円を重ねて雲状の輪郭を作り、中央を楕円で埋める。
    static const int8_t bumps[][2] = {
        {-46, -14}, {-26, -32}, {0, -38}, {26, -32}, {46, -14},
        {-52,   8}, { 52,   8}, {-30, 26}, {0, 32}, {30, 26},
    };
    for (auto& b : bumps) {
        M5.Display.fillCircle(cx + b[0], cy + b[1], 22, kColWool);
    }
    M5.Display.fillEllipse(cx, cy - 2, 52, 38, kColWool);

    // 耳（顔の左右、胴毛の前面）。
    M5.Display.fillEllipse(cx - 26, cy + 8, 8, 11, kColEar);
    M5.Display.fillEllipse(cx + 26, cy + 8, 8, 11, kColEar);

    // 顔（前面の下中央。胴毛の上に重ねる）。
    M5.Display.fillEllipse(cx, cy + 8, 24, 20, kColSheepFace);

    // 目（まばたき）と鼻。
    const float eyeOpen = eye_openness(now);
    drawSheepEye(cx - 9, cy + 3, eyeOpen);
    drawSheepEye(cx + 9, cy + 3, eyeOpen);
    M5.Display.fillTriangle(cx - 4, cy + 13, cx + 4, cy + 13, cx, cy + 18, kColSheepEye);
}

// ───────── アートシーン（Issue #34 M2：純粋生成ロジックの実機描画） ─────────
// 1画面に置く図形数。多すぎると重なりで潰れるので適度に。
constexpr int kArtShapeCount = 28;

// art_generate() が返す図形プリミティブ列を M5.Display で実際に描く（実機専用の描画責務）。
// 「何を・どこに・何色で」は純粋ロジックに委ね、ここは enum で描画 API を呼び分けるだけ。
static void drawArt(uint32_t seed) {
    M5.Display.fillScreen(kColBg);
    const std::vector<Shape> shapes = art_generate(seed, kArtShapeCount);
    for (const auto& s : shapes) {
        switch (s.kind) {
            case ShapeKind::Circle:
                M5.Display.fillCircle(s.cx, s.cy, s.size, s.color);
                break;
            case ShapeKind::Rect:
                M5.Display.fillRect(s.cx - s.size, s.cy - s.size,
                                    s.size * 2, s.size * 2, s.color);
                break;
            case ShapeKind::Triangle:
                // 中心基準で上向き三角形を描く（生成ロジックが画面内に収めている）。
                M5.Display.fillTriangle(s.cx, s.cy - s.size,
                                        s.cx - s.size, s.cy + s.size,
                                        s.cx + s.size, s.cy + s.size, s.color);
                break;
        }
    }
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

static void sheepEnter() {
    M5.Display.fillScreen(kColBg);
    g_sheepTapped = false;  // シーンに入り直したら揺れ状態をリセット
}
static void sheepUpdate(uint32_t now) {
    // タップ前は揺らさない。タップ後は経過時間から横揺れを求める（反応が切れたら 0 に戻る）。
    const int shakeX = g_sheepTapped ? sheep_shake_offset(now - g_sheepTapMs) : 0;
    drawSheep(now, shakeX);
}
static void sheepOnTap(uint32_t now) {
    g_sheepTapMs  = now;
    g_sheepTapped = true;
    playBleat();  // タップで「メェ」と鳴く
}

// --- アートシーンの状態とアダプタ ---
// 静止画なので毎フレーム描き直さない。enter / onTap で dirty を立て、その時だけ1回描く。
uint32_t g_artSeed  = 1;
bool     g_artDirty = true;

static void artEnter() {
    g_artSeed  = millis();  // 入るたびに違うアート（時刻をシードに）
    g_artDirty = true;
}
static void artUpdate(uint32_t /*now*/) {
    if (g_artDirty) {
        drawArt(g_artSeed);
        g_artDirty = false;
    }
}
static void artOnTap(uint32_t /*now*/) {
    g_artSeed  = millis();  // タップで新しいアートを生成
    g_artDirty = true;
}

// シーン表（巡回順）。ここに1要素足すだけで新テーマを増やせる。
const SceneDef kScenes[] = {
    { sheepEnter, sheepUpdate, sheepOnTap },
    { artEnter,   artUpdate,   artOnTap   },
};
constexpr int kSceneCount = static_cast<int>(sizeof(kScenes) / sizeof(kScenes[0]));
int g_sceneIdx = 0;  // 現在のシーン番号（next_scene で巡回する）

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.fillScreen(kColBg);
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
