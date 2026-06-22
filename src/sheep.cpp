#include "sheep.h"

int sheep_bob_offset(uint32_t elapsed_ms) {
    const uint32_t phase = elapsed_ms % kSheepBobPeriodMs;
    const float half = kSheepBobPeriodMs / 2.0f;
    const float a    = static_cast<float>(kSheepBobAmplitudePx);

    // 三角波： 前半で -a→+a、後半で +a→-a。eye_openness と同じ「位相→三角波」の発想。
    float off;
    if (phase < half) {
        off = -a + (2.0f * a) * (phase / half);            // -a → +a
    } else {
        off = a - (2.0f * a) * ((phase - half) / half);    // +a → -a
    }

    // 0 から離れる向きへ四捨五入（+0.5/-0.5）してから int 化する。
    return static_cast<int>(off >= 0.0f ? off + 0.5f : off - 0.5f);
}
