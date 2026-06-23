#include "voice.h"
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979f;

// 線形の振幅エンベロープ（0..1）。attack で立ち上げ、release で終端をフェードする。
// 端を 0 に落とすことで「プツッ」というクリックノイズを防ぐ。
float envelope(float prog, float attack, float release) {
    if (prog < attack)              return prog / attack;
    if (prog > 1.0f - release)      return (1.0f - prog) / release;
    return 1.0f;
}
} // namespace

int voice_baa_pcm(int16_t* out, int capacity) {
    const int n = (kBaaSamples < capacity) ? kBaaSamples : capacity;
    if (n <= 0) return 0;

    const float sr = static_cast<float>(kVoiceSampleRate);
    for (int i = 0; i < n; ++i) {
        const float t    = static_cast<float>(i) / sr;            // 経過秒
        const float prog = static_cast<float>(i) / static_cast<float>(n);  // 0..1（進行度）

        // ピッチ：520Hz → 330Hz へ下降（「メ→ェ」の下がり）に 6Hz のビブラートを掛ける。
        const float vib = 1.0f + 0.03f * sinf(2.0f * kPi * 6.0f * t);
        const float f0  = (520.0f - 190.0f * prog) * vib;

        // 倍音を足して動物っぽい“ブザー感”を出す（基音＋2,3倍音を弱めに重ねて正規化）。
        const float ph = 2.0f * kPi * f0 * t;
        float s = sinf(ph) + 0.5f * sinf(2.0f * ph) + 0.25f * sinf(3.0f * ph);
        s /= 1.75f;

        // 振幅エンベロープ（速い立ち上がり・ゆるい終端フェード）＋ヘッドルーム。
        const float v = s * envelope(prog, 0.06f, 0.30f) * 0.7f;

        int sample = static_cast<int>(v * 32767.0f);
        if (sample >  32767) sample =  32767;
        if (sample < -32768) sample = -32768;
        out[i] = static_cast<int16_t>(sample);
    }
    return n;
}
