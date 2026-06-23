#include <unity.h>
#include <vector>
#include "art.h"

void setUp(void) {}
void tearDown(void) {}

// 要求した個数ぴったり生成される
void test_generate_count() {
    auto shapes = art_generate(123u, 10);
    TEST_ASSERT_EQUAL_INT(10, static_cast<int>(shapes.size()));
}

// count <= 0 のときは空
void test_generate_zero_or_negative_is_empty() {
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(art_generate(123u, 0).size()));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(art_generate(123u, -5).size()));
}

// 同じシードなら必ず同じ結果（決定論的・再現可能）
void test_same_seed_is_deterministic() {
    auto a = art_generate(42u, 20);
    auto b = art_generate(42u, 20);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(a.size()), static_cast<int>(b.size()));
    for (size_t i = 0; i < a.size(); ++i) {
        TEST_ASSERT_TRUE(a[i].kind == b[i].kind);
        TEST_ASSERT_EQUAL_INT(a[i].cx, b[i].cx);
        TEST_ASSERT_EQUAL_INT(a[i].cy, b[i].cy);
        TEST_ASSERT_EQUAL_INT(a[i].size, b[i].size);
        TEST_ASSERT_EQUAL_UINT16(a[i].color, b[i].color);
    }
}

// 異なるシードなら結果が変わる（全一致しない）
void test_different_seed_differs() {
    auto a = art_generate(1u, 20);
    auto b = art_generate(2u, 20);
    bool any_diff = false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].cx != b[i].cx || a[i].cy != b[i].cy ||
            a[i].size != b[i].size || a[i].color != b[i].color) {
            any_diff = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(any_diff);
}

// 全図形が画面矩形内に完全に収まる（中心±size が画面内）
void test_all_within_screen_bounds() {
    auto shapes = art_generate(7u, 100);
    for (const auto& s : shapes) {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, s.cx - s.size);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, s.cy - s.size);
        TEST_ASSERT_LESS_OR_EQUAL_INT(kArtScreenW, s.cx + s.size);
        TEST_ASSERT_LESS_OR_EQUAL_INT(kArtScreenH, s.cy + s.size);
    }
}

// サイズが規定範囲内に収まる
void test_size_within_range() {
    auto shapes = art_generate(99u, 100);
    for (const auto& s : shapes) {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(kArtMinSize, s.size);
        TEST_ASSERT_LESS_OR_EQUAL_INT(kArtMaxSize, s.size);
    }
}

// 種類は3種(Triangle/Circle/Rect)のいずれか
void test_kind_is_valid() {
    auto shapes = art_generate(55u, 100);
    for (const auto& s : shapes) {
        const int k = static_cast<int>(s.kind);
        TEST_ASSERT_TRUE(k >= 0 && k <= 2);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_generate_count);
    RUN_TEST(test_generate_zero_or_negative_is_empty);
    RUN_TEST(test_same_seed_is_deterministic);
    RUN_TEST(test_different_seed_differs);
    RUN_TEST(test_all_within_screen_bounds);
    RUN_TEST(test_size_within_range);
    RUN_TEST(test_kind_is_valid);
    return UNITY_END();
}
