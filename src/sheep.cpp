#include "sheep.h"
#include <cmath>

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

int sheep_shake_offset(uint32_t elapsed_since_tap_ms) {
    if (elapsed_since_tap_ms >= kShakeDurationMs) {
        return 0;  // 反応時間を過ぎたら揺れない
    }

    // 減衰する横揺れ： 振幅を線形に 1→0 へ落としながら sin で左右に振る。
    //   t=0 では sin(0)=0 なので中心から始まり、急に飛ばない。
    const float t     = elapsed_since_tap_ms / 1000.0f;  // 秒
    const float decay = 1.0f - static_cast<float>(elapsed_since_tap_ms) / kShakeDurationMs;  // 1→0
    const float wave  = std::sin(2.0f * 3.14159265f * kShakeFreqHz * t);
    const float off   = kShakeAmplitudePx * decay * wave;

    return static_cast<int>(off >= 0.0f ? off + 0.5f : off - 0.5f);
}

int sheep_talk_offset(uint32_t elapsed_since_speak_ms) {
    // 減衰しない横揺れ： 一定振幅で sin に左右に振る（喋りのリズム）。
    //   t=0 では sin(0)=0 なので中心から始まる。停止は呼び出し側(is_speaking)が担う。
    const float t    = elapsed_since_speak_ms / 1000.0f;  // 秒
    const float wave = std::sin(2.0f * 3.14159265f * kTalkFreqHz * t);
    const float off  = kTalkAmplitudePx * wave;

    return static_cast<int>(off >= 0.0f ? off + 0.5f : off - 0.5f);
}
