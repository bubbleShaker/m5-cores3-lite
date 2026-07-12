#include <unity.h>
#include "menu.h"

void setUp(void) {}
void tearDown(void) {}

// 通常移動: 下(+1)・上(-1)
void test_moves_up_and_down() {
    TEST_ASSERT_EQUAL_INT(1, menu_move(0, 4, +1));
    TEST_ASSERT_EQUAL_INT(0, menu_move(1, 4, -1));
    TEST_ASSERT_EQUAL_INT(3, menu_move(2, 4, +1));
}

// 先頭で上へ押しても先頭に留まる（巡回しない＝クランプ）
void test_clamps_at_top() {
    TEST_ASSERT_EQUAL_INT(0, menu_move(0, 4, -1));
    TEST_ASSERT_EQUAL_INT(0, menu_move(0, 4, -5));
}

// 末尾で下へ押しても末尾に留まる（クランプ）
void test_clamps_at_bottom() {
    TEST_ASSERT_EQUAL_INT(3, menu_move(3, 4, +1));
    TEST_ASSERT_EQUAL_INT(3, menu_move(3, 4, +9));
}

// delta 0 はその場（現在位置を返す）
void test_zero_delta_stays() {
    TEST_ASSERT_EQUAL_INT(2, menu_move(2, 4, 0));
}

// 項目が1個なら常に自分自身（0）
void test_single_item_stays() {
    TEST_ASSERT_EQUAL_INT(0, menu_move(0, 1, +1));
    TEST_ASSERT_EQUAL_INT(0, menu_move(0, 1, -1));
}

// count <= 0 は安全値 0
void test_zero_or_negative_count_is_zero() {
    TEST_ASSERT_EQUAL_INT(0, menu_move(0, 0, +1));
    TEST_ASSERT_EQUAL_INT(0, menu_move(2, -1, -1));
}

// current が範囲外でも丸めてから動かす
void test_out_of_range_current_is_normalized() {
    TEST_ASSERT_EQUAL_INT(1, menu_move(5, 4, 0));   // 5 を丸めると 1、+0 → 1
    TEST_ASSERT_EQUAL_INT(2, menu_move(-3, 4, +1)); // -3 を丸めると 1、+1 → 2
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_moves_up_and_down);
    RUN_TEST(test_clamps_at_top);
    RUN_TEST(test_clamps_at_bottom);
    RUN_TEST(test_zero_delta_stays);
    RUN_TEST(test_single_item_stays);
    RUN_TEST(test_zero_or_negative_count_is_zero);
    RUN_TEST(test_out_of_range_current_is_normalized);
    return UNITY_END();
}
