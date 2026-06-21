#include <M5Unified.h>
#include "avatar.h"

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

// 変化しない部分（顔の輪郭・口）を一度だけ描く。
static void drawStaticFace() {
    M5.Display.fillScreen(kColBg);
    M5.Display.fillCircle(kFaceCx, kFaceCy, kFaceR, kColFace);
    // 口：横長の角丸矩形
    M5.Display.fillRoundRect(kFaceCx - 30, kFaceCy + 35, 60, 10, 5, kColMouth);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    drawStaticFace();
}

void loop() {
    M5.update();
    // テスト済みの純粋関数で開き具合を求め、目だけ再描画する。
    const float openness = eye_openness(millis());
    drawEye(kEyeLx, openness);
    drawEye(kEyeRx, openness);
    delay(33);  // 約30fps
}
