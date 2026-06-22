#include <unity.h>
#include "sheep.h"

void setUp(void) {}
void tearDown(void) {}

// 位相0（周期の先頭）は一番下＝ -amplitude
void test_bob_at_cycle_start_is_bottom() {
    TEST_ASSERT_EQUAL_INT(-kSheepBobAmplitudePx, sheep_bob_offset(0));
}

// 半周期で一番上＝ +amplitude
void test_bob_at_half_period_is_top() {
    TEST_ASSERT_EQUAL_INT(kSheepBobAmplitudePx,
                          sheep_bob_offset(kSheepBobPeriodMs / 2));
}

// 1/4・3/4 周期では中心（0）を通過する
void test_bob_passes_center_at_quarters() {
    TEST_ASSERT_EQUAL_INT(0, sheep_bob_offset(kSheepBobPeriodMs / 4));
    TEST_ASSERT_EQUAL_INT(0, sheep_bob_offset(kSheepBobPeriodMs * 3 / 4));
}

// 1周期ぶん進んでも同じ値（周期性）
void test_bob_is_periodic() {
    const uint32_t t = kSheepBobPeriodMs / 4;
    TEST_ASSERT_EQUAL_INT(sheep_bob_offset(t),
                          sheep_bob_offset(t + kSheepBobPeriodMs));
}

// 常に振幅の範囲内に収まる（描画がクリア枠をはみ出さない保証）
void test_bob_stays_within_amplitude() {
    for (uint32_t t = 0; t < kSheepBobPeriodMs * 2; t += 17) {
        const int v = sheep_bob_offset(t);
        TEST_ASSERT_TRUE(v >= -kSheepBobAmplitudePx && v <= kSheepBobAmplitudePx);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_bob_at_cycle_start_is_bottom);
    RUN_TEST(test_bob_at_half_period_is_top);
    RUN_TEST(test_bob_passes_center_at_quarters);
    RUN_TEST(test_bob_is_periodic);
    RUN_TEST(test_bob_stays_within_amplitude);
    return UNITY_END();
}
