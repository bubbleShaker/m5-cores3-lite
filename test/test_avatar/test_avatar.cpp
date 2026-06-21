#include <unity.h>
#include "avatar.h"

void setUp(void) {}
void tearDown(void) {}

// 周期の先頭（まばたき開始の瞬間）は、まだ目は開いている
void test_eye_open_at_cycle_start() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, eye_openness(0));
}

// まばたき窓の中間では目が完全に閉じる（開き具合 ≒ 0.0）
void test_eye_closed_at_blink_midpoint() {
    const uint32_t mid = kBlinkDurationMs / 2;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, eye_openness(mid));
}

// まばたき窓を抜けた後（周期の大半）は、ずっと開いている
void test_eye_open_outside_blink_window() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, eye_openness(kBlinkIntervalMs / 2));
}

// 1周期ぶん進んでも同じ位相になる（周期性）
void test_eye_openness_is_periodic() {
    const uint32_t mid = kBlinkDurationMs / 2;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, eye_openness(mid),
                             eye_openness(mid + kBlinkIntervalMs));
}

// 喋っていない間は口は閉じている（開き具合 = 0.0）
void test_mouth_closed_when_not_speaking() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mouth_openness(50, false));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mouth_openness(1234, false));
}

// 喋っている間、周期の先頭は閉じ、中間で開く（三角波）
void test_mouth_opens_while_speaking() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mouth_openness(0, true));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f,
                             mouth_openness(kMouthCycleMs / 2, true));
}

// 喋っている間の口の動きは周期的
void test_mouth_openness_is_periodic() {
    const uint32_t q = kMouthCycleMs / 4;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, mouth_openness(q, true),
                             mouth_openness(q + kMouthCycleMs, true));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_eye_open_at_cycle_start);
    RUN_TEST(test_eye_closed_at_blink_midpoint);
    RUN_TEST(test_eye_open_outside_blink_window);
    RUN_TEST(test_eye_openness_is_periodic);
    RUN_TEST(test_mouth_closed_when_not_speaking);
    RUN_TEST(test_mouth_opens_while_speaking);
    RUN_TEST(test_mouth_openness_is_periodic);
    return UNITY_END();
}
