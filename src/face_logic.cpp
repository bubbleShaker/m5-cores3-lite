#include "face_logic.h"

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

uint32_t speaking_duration_ms(size_t reply_bytes) {
    // バイト長に比例させた見積もり。size_t→uint32_t は下のクランプで安全側に収まる。
    const uint64_t raw = static_cast<uint64_t>(reply_bytes) * kSpeakMsPerByte;
    if (raw < kSpeakMinMs) return kSpeakMinMs;
    if (raw > kSpeakMaxMs) return kSpeakMaxMs;
    return static_cast<uint32_t>(raw);
}

bool is_speaking(uint32_t now_ms, uint32_t start_ms, uint32_t duration_ms) {
    // 開始前は false。経過が duration 未満なら喋っている最中。
    if (now_ms < start_ms) return false;
    return (now_ms - start_ms) < duration_ms;
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

FaceStyle face_style(Expression e) {
    switch (e) {
        case Expression::Happy:
            return {EyeStyle::Squint, BrowShape::Raised,    MouthShape::Smile};
        case Expression::Thinking:
            return {EyeStyle::Normal, BrowShape::Quizzical, MouthShape::Line};
        case Expression::Sad:
            return {EyeStyle::Normal, BrowShape::Worried,   MouthShape::Frown};
        case Expression::Surprised:
            return {EyeStyle::Wide,   BrowShape::Raised,    MouthShape::Round};
        case Expression::Neutral:
            return {EyeStyle::Normal, BrowShape::Flat,      MouthShape::Line};
    }
    return {EyeStyle::Normal, BrowShape::Flat, MouthShape::Line};  // 安全側
}
