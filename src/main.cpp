#include <M5Unified.h>
#include "greeting.h"

// 実機エントリ。表示文字列の生成は純粋関数 make_greeting に委譲し、
// ここは「画面に出す」ハード依存部だけに責務を絞る。
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 10);
    M5.Display.println(make_greeting("CoreS3-Lite").c_str());
}

void loop() {
    M5.update();
    delay(16);
}
