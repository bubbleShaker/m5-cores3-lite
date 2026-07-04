#include <unity.h>
#include <cstdint>
#include <vector>
#include "envelope.h"

void setUp(void) {}
void tearDown(void) {}

// 空入力・無音は全 bucket が 0（粒子が出ない側に倒れる）
void test_empty_and_null_are_zero() {
    uint8_t out[8];
    TEST_ASSERT_EQUAL_INT(8, voice_envelope(nullptr, 0, out, 8));
    for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL_UINT8(0, out[i]);

    std::vector<int16_t> silence(1000, 0);
    voice_envelope(silence.data(), 1000, out, 8);
    for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL_UINT8(0, out[i]);
}

// 一定振幅なら全 bucket が等しく、正規化でピーク=255 になる
void test_constant_amplitude_normalizes_to_peak() {
    std::vector<int16_t> pcm(800, 1000);
    uint8_t out[8];
    voice_envelope(pcm.data(), 800, out, 8);
    for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL_UINT8(255, out[i]);
}

// 前半 静か / 後半 大きい → 前半の bucket の方が小さい（実音声の大小に連動）
void test_ramp_reflects_loudness() {
    std::vector<int16_t> pcm;
    for (int i = 0; i < 400; ++i) pcm.push_back(100);    // 静か
    for (int i = 0; i < 400; ++i) pcm.push_back(8000);   // 大きい
    uint8_t out[8];
    const int n = voice_envelope(pcm.data(), (int)pcm.size(), out, 8);
    TEST_ASSERT_EQUAL_INT(8, n);
    TEST_ASSERT_LESS_THAN_UINT8(out[7], out[0]);  // 先頭 < 末尾
    TEST_ASSERT_EQUAL_UINT8(255, out[7]);          // 最大区間がピーク
}

// 負値も絶対値で扱う（対称な振幅は正側と同じ強さになる）
void test_abs_value_used() {
    std::vector<int16_t> pos(400, 5000);
    std::vector<int16_t> neg(400, -5000);
    uint8_t a[4], b[4];
    voice_envelope(pos.data(), 400, a, 4);
    voice_envelope(neg.data(), 400, b, 4);
    for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_UINT8(a[i], b[i]);
}

// buckets が上限を超えても丸めて安全に返す（バッファ破壊しない）
void test_buckets_clamped() {
    std::vector<int16_t> pcm(500, 3000);
    std::vector<uint8_t> out(kEnvMaxBuckets + 10, 0xAB);
    const int n = voice_envelope(pcm.data(), 500, out.data(), kEnvMaxBuckets + 10);
    TEST_ASSERT_EQUAL_INT(kEnvMaxBuckets, n);
    TEST_ASSERT_EQUAL_UINT8(0xAB, out[kEnvMaxBuckets]);  // 上限超の領域は触っていない
}

// buckets<=0 は 0 を返し何も書かない
void test_non_positive_buckets() {
    std::vector<int16_t> pcm(100, 1000);
    uint8_t out[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(0, voice_envelope(pcm.data(), 100, out, 0));
    TEST_ASSERT_EQUAL_UINT8(1, out[0]);  // 未書き込み
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_and_null_are_zero);
    RUN_TEST(test_constant_amplitude_normalizes_to_peak);
    RUN_TEST(test_ramp_reflects_loudness);
    RUN_TEST(test_abs_value_used);
    RUN_TEST(test_buckets_clamped);
    RUN_TEST(test_non_positive_buckets);
    return UNITY_END();
}
