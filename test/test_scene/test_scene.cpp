#include <unity.h>
#include "scene.h"

void setUp(void) {}
void tearDown(void) {}

// 通常の前進: 0 → 1 → 2
void test_advances_forward() {
    TEST_ASSERT_EQUAL_INT(1, next_scene(0, 3));
    TEST_ASSERT_EQUAL_INT(2, next_scene(1, 3));
}

// 末尾の次は先頭へ折り返す（巡回）
void test_wraps_to_first() {
    TEST_ASSERT_EQUAL_INT(0, next_scene(2, 3));
}

// 一巡すると元に戻る
void test_full_cycle_returns_to_start() {
    int i = 0;
    for (int k = 0; k < 3; ++k) i = next_scene(i, 3);
    TEST_ASSERT_EQUAL_INT(0, i);
}

// シーンが1個なら常に自分自身（0）
void test_single_scene_stays() {
    TEST_ASSERT_EQUAL_INT(0, next_scene(0, 1));
}

// count <= 0 は安全値 0 を返す
void test_zero_or_negative_count_is_zero() {
    TEST_ASSERT_EQUAL_INT(0, next_scene(0, 0));
    TEST_ASSERT_EQUAL_INT(0, next_scene(2, -1));
}

// current が範囲外でも正規化して進む
void test_out_of_range_current_is_normalized() {
    TEST_ASSERT_EQUAL_INT(0, next_scene(5, 3));   // 5%3=2 → 次は 0
    TEST_ASSERT_EQUAL_INT(1, next_scene(3, 3));   // 3%3=0 → 次は 1
    TEST_ASSERT_EQUAL_INT(0, next_scene(-1, 3));  // -1 → 2 → 次は 0
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_advances_forward);
    RUN_TEST(test_wraps_to_first);
    RUN_TEST(test_full_cycle_returns_to_start);
    RUN_TEST(test_single_scene_stays);
    RUN_TEST(test_zero_or_negative_count_is_zero);
    RUN_TEST(test_out_of_range_current_is_normalized);
    return UNITY_END();
}
