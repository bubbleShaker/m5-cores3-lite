#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>   // 中継サーバへの HTTP POST（ESP32 標準）
#include <ArduinoJson.h>  // リクエスト body の安全な組み立て
#include <string>
#include "avatar.h"
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
constexpr int kEyeHalfHMax = 22;  // 開ききった時の半分の高さ

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

// 色（M5GFX の RGB565）。
constexpr uint16_t kColBg    = TFT_BLACK;
constexpr uint16_t kColFace  = 0xFE19;  // 肌色っぽいピンク
constexpr uint16_t kColEye   = TFT_BLACK;
constexpr uint16_t kColMouth = 0x8000;  // 暗い赤

// 目を1つ描く。openness(0.0..1.0) に応じて目の高さを変える。
// まず目の最大領域を顔色で塗りつぶしてから、開いている分だけ黒目を描く（ちらつき抑制）。
static void drawEye(int cx, float openness) {
    M5.Display.fillRect(cx - kEyeHalfW, kEyeY - kEyeHalfHMax,
                        kEyeHalfW * 2, kEyeHalfHMax * 2, kColFace);

    const int halfH = static_cast<int>(kEyeHalfHMax * openness);
    if (halfH <= 1) {
        // ほぼ閉じている → 横一本の線で「とじ目」を表現
        M5.Display.fillRect(cx - kEyeHalfW, kEyeY - 1, kEyeHalfW * 2, 2, kColEye);
    } else {
        M5.Display.fillRect(cx - kEyeHalfW, kEyeY - halfH, kEyeHalfW * 2, halfH * 2, kColEye);
    }
}

// 口を描く。openness(0.0..1.0) に応じて口の高さを変える。
// まず口の最大領域を顔色でクリアしてから、開いている分だけ口を描く（ちらつき抑制）。
static void drawMouth(float openness) {
    M5.Display.fillRect(kMouthCx - kMouthHalfW, kMouthCy - kMouthHalfHMax,
                        kMouthHalfW * 2, kMouthHalfHMax * 2, kColFace);

    const int halfH = static_cast<int>(kMouthHalfHMax * openness);
    if (halfH <= 1) {
        // ほぼ閉じている → 横一本の線
        M5.Display.fillRect(kMouthCx - kMouthHalfW, kMouthCy - 1,
                            kMouthHalfW * 2, 2, kColMouth);
    } else {
        // 開いている → 楕円で口の中を表現
        M5.Display.fillEllipse(kMouthCx, kMouthCy, kMouthHalfW, halfH, kColMouth);
    }
}

// 変化しない部分（顔の輪郭）を一度だけ描く。
static void drawStaticFace() {
    M5.Display.fillScreen(kColBg);
    M5.Display.fillCircle(kFaceCx, kFaceCy, kFaceR, kColFace);
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

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    drawStaticFace();

    // Wi-Fi 接続を開始（非同期。完了は loop でポーリングする）。
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    g_wifiBeginMs = millis();
    drawWifiStatus(WifiState::Connecting);
}

void loop() {
    M5.update();
    const uint32_t now = millis();

    // Wi-Fi 状態：変化した時だけ文言を描き直す（ちらつき・無駄描画を抑制）。
    static WifiState lastWifi = WifiState::Connecting;
    const WifiState wifi = currentWifiState(now);
    if (wifi != lastWifi) {
        drawWifiStatus(wifi);
        lastWifi = wifi;
    }

    // 起動後、Wi-Fi 接続できたら一度だけ中継サーバへ挨拶を問い合わせる（最小トリガ）。
    if (wifi == WifiState::Connected && !g_hasReply) {
        fetchGreeting();  // ブロッキング HTTP（この最小構成では許容）
    }

    // 対話描画：到着時に一度、その後は表情の自動復帰(active_expression)で変化した時だけ描き直す。
    if (g_hasReply) {
        static bool dialogDrawn = false;
        static Expression lastExpr = Expression::Neutral;
        const Expression activeExpr =
            active_expression(g_requestedExpr, now - g_exprRequestMs);
        if (!dialogDrawn || activeExpr != lastExpr) {
            drawDialog(g_reply, activeExpr);
            dialogDrawn = true;
            lastExpr = activeExpr;
        }
    }

    // まばたき：テスト済みの純粋関数で開き具合を求め、目だけ再描画。
    const float eye = eye_openness(now);
    drawEye(kEyeLx, eye);
    drawEye(kEyeRx, eye);

    // 口パク：デモ用スケジュールで speaking を決め、口を再描画。
    // speaking の実トリガー（マイク/対話）は後続 Issue で差し替える。
    const bool speaking = (now % kSpeakPeriodMs) < kSpeakOnMs;
    drawMouth(mouth_openness(now, speaking));

    delay(33);  // 約30fps
}
