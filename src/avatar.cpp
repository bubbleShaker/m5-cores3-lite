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

float mouth_openness(uint32_t elapsed_ms, bool speaking) {
    if (!speaking) {
        return 0.0f;
    }

    // 喋っている間は三角波： 前半で 0.0→1.0、後半で 1.0→0.0。
    const uint32_t phase = elapsed_ms % kMouthCycleMs;
    const float half = kMouthCycleMs / 2.0f;
    if (phase <= half) {
        return phase / half;          // 閉 → 開
    }
    return 1.0f - (phase - half) / half;  // 開 → 閉
}

Expression parse_expression(const std::string& name) {
    if (name == "happy")     return Expression::Happy;
    if (name == "thinking")  return Expression::Thinking;
    if (name == "sad")       return Expression::Sad;
    if (name == "surprised") return Expression::Surprised;
    return Expression::Neutral;  // "neutral" と未知の値はここに集約
}

Expression active_expression(Expression requested, uint32_t elapsed_since_request_ms) {
    if (requested == Expression::Neutral) {
        return Expression::Neutral;
    }
    if (elapsed_since_request_ms < kExpressionHoldMs) {
        return requested;
    }
    return Expression::Neutral;  // ホールド時間を過ぎたら自然な顔に戻す
}
