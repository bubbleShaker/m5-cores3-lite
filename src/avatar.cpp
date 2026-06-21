#include "avatar.h"

float eye_openness(uint32_t elapsed_ms) {
    // 周期内の位相。瞬き窓(kBlinkDurationMs)の外ならずっと開いたまま。
    const uint32_t phase = elapsed_ms % kBlinkIntervalMs;
    if (phase >= kBlinkDurationMs) {
        return 1.0f;
    }

    // 瞬き窓の中は三角波： 前半で 1.0→0.0、後半で 0.0→1.0。
    const float half = kBlinkDurationMs / 2.0f;
    if (phase <= half) {
        return 1.0f - phase / half;   // 開 → 閉
    }
    return (phase - half) / half;     // 閉 → 開
}
