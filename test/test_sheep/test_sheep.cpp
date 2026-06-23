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

// タップ直後（t=0）は中心から始まる（急に飛ばない）
void test_shake_starts_at_center() {
    TEST_ASSERT_EQUAL_INT(0, sheep_shake_offset(0));
}

// 反応時間を過ぎたら揺れは止まる（0 に戻る）
void test_shake_stops_after_duration() {
    TEST_ASSERT_EQUAL_INT(0, sheep_shake_offset(kShakeDurationMs));
    TEST_ASSERT_EQUAL_INT(0, sheep_shake_offset(kShakeDurationMs + 100));
}

// 反応中は常に振幅の範囲内（クリア枠をはみ出さない保証）
void test_shake_stays_within_amplitude() {
    for (uint32_t t = 0; t < kShakeDurationMs; t += 3) {
        const int v = sheep_shake_offset(t);
        TEST_ASSERT_TRUE(v >= -kShakeAmplitudePx && v <= kShakeAmplitudePx);
    }
}

// 反応中のどこかで実際に揺れる（非ゼロになる瞬間がある＝動いている証拠）
void test_shake_is_nonzero_somewhere() {
    bool moved = false;
    for (uint32_t t = 0; t < kShakeDurationMs; t += 3) {
        if (sheep_shake_offset(t) != 0) { moved = true; break; }
    }
    TEST_ASSERT_TRUE(moved);
}

// ── 発話揺れ（②-3a / Issue #23） ──
// t=0 は中心から始まる（急に飛ばない）
void test_talk_starts_at_center() {
    TEST_ASSERT_EQUAL_INT(0, sheep_talk_offset(0));
}

// shake と違い減衰しない：時間が経っても止まらず、後半でも揺れる瞬間がある
void test_talk_does_not_decay() {
    bool moved_late = false;
    for (uint32_t t = 3000; t < 4000; t += 3) {  // 十分経過した後でも
        if (sheep_talk_offset(t) != 0) { moved_late = true; break; }
    }
    TEST_ASSERT_TRUE(moved_late);
}

// 常に振幅の範囲内（クリア枠をはみ出さない保証）
void test_talk_stays_within_amplitude() {
    for (uint32_t t = 0; t < 4000; t += 3) {
        const int v = sheep_talk_offset(t);
        TEST_ASSERT_TRUE(v >= -kTalkAmplitudePx && v <= kTalkAmplitudePx);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_bob_at_cycle_start_is_bottom);
    RUN_TEST(test_bob_at_half_period_is_top);
    RUN_TEST(test_bob_passes_center_at_quarters);
    RUN_TEST(test_bob_is_periodic);
    RUN_TEST(test_bob_stays_within_amplitude);
    RUN_TEST(test_shake_starts_at_center);
    RUN_TEST(test_shake_stops_after_duration);
    RUN_TEST(test_shake_stays_within_amplitude);
    RUN_TEST(test_shake_is_nonzero_somewhere);
    RUN_TEST(test_talk_starts_at_center);
    RUN_TEST(test_talk_does_not_decay);
    RUN_TEST(test_talk_stays_within_amplitude);
    return UNITY_END();
}
