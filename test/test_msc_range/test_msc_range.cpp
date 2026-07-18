#include <unity.h>
#include "msc_range.h"

void setUp(void) {}
void tearDown(void) {}

// 32GB カード相当（512B セクタ）を想定した現実的なセクタ数。
static const uint32_t kSectors = 61071360;

// 通常の読み書き要求は通る
void test_normal_request_ok() {
    TEST_ASSERT_TRUE(msc_range_ok(0, 1, kSectors));
    TEST_ASSERT_TRUE(msc_range_ok(0, 8, kSectors));          // MSC バッファ 4096B = 8 セクタ
    TEST_ASSERT_TRUE(msc_range_ok(1000, 8, kSectors));
}

// 最終セクタちょうどに収まる要求は通る（オフバイワン検出）
void test_exact_end_is_ok() {
    TEST_ASSERT_TRUE(msc_range_ok(kSectors - 1, 1, kSectors));
    TEST_ASSERT_TRUE(msc_range_ok(kSectors - 8, 8, kSectors));
}

// 1 セクタでもはみ出したら弾く
void test_one_past_end_rejected() {
    TEST_ASSERT_FALSE(msc_range_ok(kSectors - 7, 8, kSectors));
    TEST_ASSERT_FALSE(msc_range_ok(kSectors, 1, kSectors));
    TEST_ASSERT_FALSE(msc_range_ok(kSectors + 1, 1, kSectors));
}

// 0 セクタ要求は不正として弾く
void test_zero_count_rejected() {
    TEST_ASSERT_FALSE(msc_range_ok(0, 0, kSectors));
    TEST_ASSERT_FALSE(msc_range_ok(100, 0, kSectors));
}

// 本命の回帰テスト：加算オーバーフローで境界チェックを素通りさせない。
// lba + count を 32bit で足すとラップして小さな値になり、素通り→セクタ0(MBR)破壊に化ける。
// msc_range_ok は減算比較なのでラップしない。
void test_overflow_does_not_wrap_through() {
    TEST_ASSERT_FALSE(msc_range_ok(0xFFFFFFFFu, 8, kSectors));
    TEST_ASSERT_FALSE(msc_range_ok(0xFFFFFFF8u, 16, kSectors));
    // 足すとちょうど 0 になる組み合わせ（最も危険なパターン）
    TEST_ASSERT_FALSE(msc_range_ok(0xFFFFFF00u, 0x100u, kSectors));
}

// カードが極端に小さい/セクタ数 0 の異常時も安全側に倒れる
void test_degenerate_sector_count() {
    TEST_ASSERT_FALSE(msc_range_ok(0, 1, 0));
    TEST_ASSERT_TRUE(msc_range_ok(0, 1, 1));
    TEST_ASSERT_FALSE(msc_range_ok(1, 1, 1));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_normal_request_ok);
    RUN_TEST(test_exact_end_is_ok);
    RUN_TEST(test_one_past_end_rejected);
    RUN_TEST(test_zero_count_rejected);
    RUN_TEST(test_overflow_does_not_wrap_through);
    RUN_TEST(test_degenerate_sector_count);
    return UNITY_END();
}
