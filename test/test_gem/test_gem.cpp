#include <unity.h>
#include <cstring>
#include "gem.h"

void setUp(void) {}
void tearDown(void) {}

// 図鑑には1件以上の宝石がある
void test_count_is_positive() {
    TEST_ASSERT_GREATER_THAN_INT(0, gem_count());
}

// 各宝石のフィールドが全て非空（データ抜けが無い）
void test_all_fields_nonempty() {
    for (int i = 0; i < gem_count(); ++i) {
        const Gem* g = gem_at(i);
        TEST_ASSERT_NOT_NULL(g);
        TEST_ASSERT_NOT_NULL(g->name);
        TEST_ASSERT_NOT_NULL(g->locality);
        TEST_ASSERT_NOT_NULL(g->formula);
        TEST_ASSERT_NOT_NULL(g->elements);
        TEST_ASSERT_GREATER_THAN_size_t(0, strlen(g->name));
        TEST_ASSERT_GREATER_THAN_size_t(0, strlen(g->locality));
        TEST_ASSERT_GREATER_THAN_size_t(0, strlen(g->formula));
        TEST_ASSERT_GREATER_THAN_size_t(0, strlen(g->elements));
    }
}

// 有効な index は対応する宝石をそのまま返す
void test_in_range_returns_same() {
    TEST_ASSERT_EQUAL_PTR(gem_at(0), gem_at(0));
    // 先頭と末尾が別物であること（テーブルが潰れていない）。
    // ポインタの非等価ではなく「名前の値」で比較する（意図に忠実・切り詰めの曖昧さを避ける）。
    TEST_ASSERT_TRUE(strcmp(gem_at(0)->name, gem_at(gem_count() - 1)->name) != 0);
}

// 範囲を超えた index は巡回して正規化される（gem_count を足した index と同じ宝石）
void test_out_of_range_wraps() {
    const int n = gem_count();
    TEST_ASSERT_EQUAL_PTR(gem_at(0), gem_at(n));       // n → 0
    TEST_ASSERT_EQUAL_PTR(gem_at(1), gem_at(n + 1));   // n+1 → 1
}

// 負の index も巡回して正規化される（末尾側へ折り返す）
void test_negative_index_wraps() {
    const int n = gem_count();
    TEST_ASSERT_EQUAL_PTR(gem_at(n - 1), gem_at(-1));  // -1 → 末尾
    TEST_ASSERT_EQUAL_PTR(gem_at(0), gem_at(-n));      // -n → 0
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_count_is_positive);
    RUN_TEST(test_all_fields_nonempty);
    RUN_TEST(test_in_range_returns_same);
    RUN_TEST(test_out_of_range_wraps);
    RUN_TEST(test_negative_index_wraps);
    return UNITY_END();
}
