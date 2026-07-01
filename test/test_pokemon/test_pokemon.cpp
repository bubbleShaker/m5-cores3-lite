#include <unity.h>
#include <string>
#include "pokemon.h"

void setUp(void) {}
void tearDown(void) {}

// --- parse_pokemon_info（中継 /pokemon/info 応答のパース） ---

// 正しい JSON は全フィールドがそのまま写る
void test_parse_info_valid() {
    Pokemon p = parse_pokemon_info(R"({
        "id": 1,
        "name_ja": "フシギダネ",
        "name_en": "bulbasaur",
        "category_ja": "たねポケモン",
        "types": ["grass", "poison"],
        "desc_ja": "うまれたときから せなかに ふしぎな タネが うえてある。"
    })");
    TEST_ASSERT_EQUAL_INT(1, p.id);
    TEST_ASSERT_EQUAL_STRING("フシギダネ", p.name_ja.c_str());
    TEST_ASSERT_EQUAL_STRING("bulbasaur", p.name_en.c_str());
    TEST_ASSERT_EQUAL_STRING("たねポケモン", p.category_ja.c_str());
    TEST_ASSERT_EQUAL_STRING("grass, poison", p.types.c_str());
    TEST_ASSERT_EQUAL_STRING("うまれたときから せなかに ふしぎな タネが うえてある。",
                             p.desc_ja.c_str());
}

// 単一タイプは区切り無しの1本になる
void test_parse_info_single_type() {
    Pokemon p = parse_pokemon_info(
        R"({"id":25,"name_ja":"ピカチュウ","types":["electric"]})");
    TEST_ASSERT_EQUAL_STRING("electric", p.types.c_str());
}

// types が空配列なら types も空文字（末尾に区切りを残さない）
void test_parse_info_empty_types() {
    Pokemon p = parse_pokemon_info(R"({"id":132,"name_ja":"メタモン","types":[]})");
    TEST_ASSERT_EQUAL_STRING("", p.types.c_str());
}

// types 配列内の非文字列（数値・null）は黙って飛ばす
void test_parse_info_types_non_string_ignored() {
    Pokemon p = parse_pokemon_info(
        R"({"id":1,"types":["grass", 5, null, "poison"]})");
    TEST_ASSERT_EQUAL_STRING("grass, poison", p.types.c_str());
}

// types が配列でない（文字列や数値）場合も空文字に倒れる（契約: 配列以外は空）
void test_parse_info_types_not_array() {
    Pokemon p1 = parse_pokemon_info(R"({"id":1,"types":"grass"})");
    TEST_ASSERT_EQUAL_STRING("", p1.types.c_str());
    Pokemon p2 = parse_pokemon_info(R"({"id":1,"types":5})");
    TEST_ASSERT_EQUAL_STRING("", p2.types.c_str());
}

// キー欠落は安全側の既定値（id=0・空文字）で埋まる
void test_parse_info_missing_keys() {
    Pokemon p = parse_pokemon_info(R"({"name_ja":"ヒトカゲ"})");
    TEST_ASSERT_EQUAL_INT(0, p.id);
    TEST_ASSERT_EQUAL_STRING("ヒトカゲ", p.name_ja.c_str());
    TEST_ASSERT_EQUAL_STRING("", p.name_en.c_str());
    TEST_ASSERT_EQUAL_STRING("", p.category_ja.c_str());
    TEST_ASSERT_EQUAL_STRING("", p.types.c_str());
    TEST_ASSERT_EQUAL_STRING("", p.desc_ja.c_str());
}

// パース不能なテキストは空の Pokemon（id=0）で落ちない
void test_parse_info_invalid_json() {
    Pokemon p = parse_pokemon_info("ただのテキスト");
    TEST_ASSERT_EQUAL_INT(0, p.id);
    TEST_ASSERT_EQUAL_STRING("", p.name_ja.c_str());
    TEST_ASSERT_EQUAL_STRING("", p.types.c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_info_valid);
    RUN_TEST(test_parse_info_single_type);
    RUN_TEST(test_parse_info_empty_types);
    RUN_TEST(test_parse_info_types_non_string_ignored);
    RUN_TEST(test_parse_info_types_not_array);
    RUN_TEST(test_parse_info_missing_keys);
    RUN_TEST(test_parse_info_invalid_json);
    return UNITY_END();
}
