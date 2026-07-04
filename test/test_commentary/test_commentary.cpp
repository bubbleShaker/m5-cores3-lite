#include <unity.h>
#include <string>
#include "commentary.h"

void setUp(void) {}
void tearDown(void) {}

// 文字列 haystack が needle を含むことを主張する小ヘルパ。
static void assert_contains(const std::string& haystack, const std::string& needle) {
    TEST_ASSERT_TRUE_MESSAGE(haystack.find(needle) != std::string::npos, needle.c_str());
}

// --- gem_commentary ---

// 全フィールドが揃った宝石は「名前です。産地は…組成は…主な元素は…です。」の形になる
void test_gem_full() {
    Gem g{"ルビー", "ミャンマー", "Al2O3 (Cr)", "Al / O（発色:Cr）", 0};
    std::string s = gem_commentary(g);
    TEST_ASSERT_EQUAL_STRING(
        "ルビーです。産地はミャンマー、組成はAl2O3 (Cr)、主な元素はAl / O（発色:Cr）です。",
        s.c_str());
}

// 実データ（gem_at）で全件、名前とラベルと語尾が入っていること（テーブルとの結合確認）
void test_gem_from_table() {
    for (int i = 0; i < gem_count(); ++i) {
        const Gem* g = gem_at(i);
        std::string s = gem_commentary(*g);
        assert_contains(s, g->name);
        assert_contains(s, "産地は");
        assert_contains(s, "です。");
    }
}

// 一部フィールドが空でも落とさず、空の項目だけ省いて連結する
void test_gem_partial_fields() {
    Gem g{"謎の石", "", "SiO2", "", 0};
    std::string s = gem_commentary(g);
    // 産地・元素は省かれ、組成だけが残る
    TEST_ASSERT_EQUAL_STRING("謎の石です。組成はSiO2です。", s.c_str());
}

// 名前が空なら既定の呼称へフォールバックし、事実が無ければ本体だけ返す
void test_gem_empty_falls_back() {
    Gem g{"", "", "", "", 0};
    std::string s = gem_commentary(g);
    TEST_ASSERT_EQUAL_STRING("この宝石です。", s.c_str());
}

// name が nullptr でも落ちない（Gem は const char*。安全側に畳む）
void test_gem_null_pointer_safe() {
    Gem g{nullptr, nullptr, nullptr, nullptr, 0};
    std::string s = gem_commentary(g);
    TEST_ASSERT_EQUAL_STRING("この宝石です。", s.c_str());
}

// --- pokemon_commentary ---

// 全フィールドが揃ったポケモンは「名前、分類です。タイプは…。説明」の形になる
void test_pokemon_full() {
    Pokemon p;
    p.id = 1;
    p.name_ja = "フシギダネ";
    p.category_ja = "たねポケモン";
    p.types = "grass, poison";
    p.desc_ja = "うまれたときからせなかにタネがある。";
    std::string s = pokemon_commentary(p);
    TEST_ASSERT_EQUAL_STRING(
        "フシギダネ、たねポケモンです。タイプはgrass, poison。うまれたときからせなかにタネがある。",
        s.c_str());
}

// 分類・タイプ・説明が空でも名前と語尾だけで成立する
void test_pokemon_name_only() {
    Pokemon p;
    p.name_ja = "ミュウ";
    std::string s = pokemon_commentary(p);
    TEST_ASSERT_EQUAL_STRING("ミュウです。", s.c_str());
}

// 名前が空（パース失敗の既定値）なら既定の呼称へフォールバック
void test_pokemon_empty_falls_back() {
    Pokemon p;  // 全て既定値（name_ja="")
    std::string s = pokemon_commentary(p);
    TEST_ASSERT_EQUAL_STRING("このポケモンです。", s.c_str());
}

// 決定論的：同じ入力なら必ず同じ文字列
void test_deterministic() {
    Gem g{"エメラルド", "コロンビア", "Be3Al2Si6O18 (Cr)", "Be / Al / Si / O", 0};
    TEST_ASSERT_EQUAL_STRING(gem_commentary(g).c_str(), gem_commentary(g).c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_gem_full);
    RUN_TEST(test_gem_from_table);
    RUN_TEST(test_gem_partial_fields);
    RUN_TEST(test_gem_empty_falls_back);
    RUN_TEST(test_gem_null_pointer_safe);
    RUN_TEST(test_pokemon_full);
    RUN_TEST(test_pokemon_name_only);
    RUN_TEST(test_pokemon_empty_falls_back);
    RUN_TEST(test_deterministic);
    return UNITY_END();
}
