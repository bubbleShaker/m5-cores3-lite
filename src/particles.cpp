#include "particles.h"
#include <cmath>

int particle_ring(Particle* out, int capacity, int count,
                  int cx, int cy, int baseRadius, float t, float intensity) {
    if (capacity <= 0 || count <= 0) return 0;
    if (count > capacity) count = capacity;
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;

    constexpr float kTwoPi = 6.2831853f;
    for (int i = 0; i < count; ++i) {
        const float base = kTwoPi * i / count;   // 粒の基準角（等間隔）
        const float ang  = base + t * 0.6f;       // ゆっくり公転
        // 粒ごとに位相をずらした波打ち（-1..1）。リングが均一な円でなく脈打つように見せる。
        const float wob  = sinf(base * 3.0f + t * 2.0f);

        // 半径：intensity が大きいほど外へ膨らみ、波打ちで凹凸が出る。
        const float r = baseRadius + intensity * (6.0f + 5.0f * wob);
        out[i].x = cx + static_cast<int>(lroundf(cosf(ang) * r));
        out[i].y = cy + static_cast<int>(lroundf(sinf(ang) * r));

        // 明るさ：intensity を主軸に、個体位相で 0.6..1.0 倍の変調（きらめき）。
        const float b = intensity * (0.6f + 0.4f * (0.5f + 0.5f * wob));
        int lvl = static_cast<int>(lroundf(b * 255.0f));
        if (lvl < 0) lvl = 0;
        if (lvl > 255) lvl = 255;
        out[i].level  = static_cast<uint8_t>(lvl);
        out[i].radius = static_cast<uint8_t>(2 + static_cast<int>(lroundf(intensity * 2.0f)));
    }
    return count;
}
