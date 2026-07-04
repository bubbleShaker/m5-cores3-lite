#include "envelope.h"

int voice_envelope(const int16_t* pcm, int samples, uint8_t* out, int buckets) {
    if (buckets <= 0) return 0;
    if (buckets > kEnvMaxBuckets) buckets = kEnvMaxBuckets;

    // 空入力・無音は全 0（呼び出し側で「強さ 0＝粒子が出ない」に落ちる）。
    if (!pcm || samples <= 0) {
        for (int i = 0; i < buckets; ++i) out[i] = 0;
        return buckets;
    }

    // 各 bucket の平均絶対振幅（|int16| は最大 32768 なので平均も uint16 に収まる）。
    uint16_t mean[kEnvMaxBuckets];
    uint16_t peak = 0;
    for (int b = 0; b < buckets; ++b) {
        // 区間 [start, end) を samples から等分に切り出す（端数は前寄りに割れる）。
        const long start = static_cast<long>(b) * samples / buckets;
        const long end   = static_cast<long>(b + 1) * samples / buckets;
        const long n     = end - start;

        uint32_t sum = 0;  // 20k サンプル×32768 でも uint32 に収まる（1MB WAV 前提で有界）
        for (long i = start; i < end; ++i) {
            int v = pcm[i];
            if (v < 0) v = -v;
            sum += static_cast<uint32_t>(v);
        }
        const uint16_t m = (n > 0) ? static_cast<uint16_t>(sum / static_cast<uint32_t>(n)) : 0;
        mean[b] = m;
        if (m > peak) peak = m;
    }

    // ピークが 255 になる相対正規化（声の抑揚を明るさの全域に写す）。無音は 0。
    for (int b = 0; b < buckets; ++b) {
        out[b] = (peak > 0)
                     ? static_cast<uint8_t>(static_cast<uint32_t>(mean[b]) * 255u / peak)
                     : 0;
    }
    return buckets;
}
