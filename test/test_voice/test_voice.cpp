#include <unity.h>
#include <cstdlib>
#include <cstring>
#include "voice.h"

// 音声 M1（Issue #44）の PCM 合成を検証する。
// 浮動小数の最終ビットは実機=xtensa と native=x86 で一致しないため、特定サンプル値ではなく
// 「長さ・範囲・端が0・非無音・決定論」という性質で担保する。

void setUp(void) {}
void tearDown(void) {}

// capacity が十分なら kBaaSamples 個ぴったり書き込む
void test_returns_full_length() {
    static int16_t buf[kBaaSamples + 100];
    TEST_ASSERT_EQUAL_INT(kBaaSamples, voice_baa_pcm(buf, kBaaSamples + 100));
}

// capacity が小さいときはそれを超えない
void test_respects_capacity() {
    static int16_t buf[kBaaSamples];
    TEST_ASSERT_EQUAL_INT(100, voice_baa_pcm(buf, 100));
}

// capacity <= 0 は 0 サンプル（何も書かない）
void test_zero_capacity_is_safe() {
    int16_t dummy = 123;
    TEST_ASSERT_EQUAL_INT(0, voice_baa_pcm(&dummy, 0));
}

// 端（最初と最後）はエンベロープでほぼ無音＝クリックノイズが出ない
void test_envelope_edges_near_zero() {
    static int16_t buf[kBaaSamples];
    const int n = voice_baa_pcm(buf, kBaaSamples);
    TEST_ASSERT_EQUAL_INT16(0, buf[0]);            // attack 開始は厳密に 0
    TEST_ASSERT_TRUE(std::abs(buf[n - 1]) < 2000); // release 終端はほぼ 0
}

// 中間は十分な音量がある（無音ではない＝ちゃんと波形が入っている）
void test_has_audible_energy() {
    static int16_t buf[kBaaSamples];
    const int n = voice_baa_pcm(buf, kBaaSamples);
    int16_t peak = 0;
    for (int i = 0; i < n; ++i) {
        const int16_t a = static_cast<int16_t>(std::abs(buf[i]));
        if (a > peak) peak = a;
    }
    TEST_ASSERT_TRUE(peak > 10000);  // 16bit のうち十分な振幅
}

// 同じ引数なら必ず同じ波形（決定論的）
void test_is_deterministic() {
    static int16_t a[kBaaSamples];
    static int16_t b[kBaaSamples];
    const int na = voice_baa_pcm(a, kBaaSamples);
    const int nb = voice_baa_pcm(b, kBaaSamples);
    TEST_ASSERT_EQUAL_INT(na, nb);
    TEST_ASSERT_EQUAL_INT(0, std::memcmp(a, b, na * sizeof(int16_t)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_returns_full_length);
    RUN_TEST(test_respects_capacity);
    RUN_TEST(test_zero_capacity_is_safe);
    RUN_TEST(test_envelope_edges_near_zero);
    RUN_TEST(test_has_audible_energy);
    RUN_TEST(test_is_deterministic);
    return UNITY_END();
}
