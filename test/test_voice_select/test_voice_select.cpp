#include <unity.h>
#include <cstring>
#include "voice_select.h"

void setUp(void) {}
void tearDown(void) {}

// 候補が2体以上あり、既定(index0)はずんだもん(speaker 3・tts.ts の DEFAULT_SPEAKER と一致)
void test_default_is_zundamon() {
    TEST_ASSERT_GREATER_THAN_INT(1, voice_option_count());
    TEST_ASSERT_EQUAL_INT(3, voice_speaker_at(0));
    TEST_ASSERT_EQUAL_STRING("ずんだもん", voice_name_at(0));
}

// next で1つ進み、末尾の次は先頭へ巡回する
void test_next_cycles() {
    const int n = voice_option_count();
    TEST_ASSERT_EQUAL_INT(1, voice_next(0));
    TEST_ASSERT_EQUAL_INT(0, voice_next(n - 1));  // 末尾→先頭
}

// prev で1つ戻り、先頭の前は末尾へ巡回する
void test_prev_cycles() {
    const int n = voice_option_count();
    TEST_ASSERT_EQUAL_INT(n - 1, voice_prev(0));  // 先頭→末尾
    TEST_ASSERT_EQUAL_INT(0, voice_prev(1));
}

// 範囲外インデックスでも巡回で正規化され、落ちない（堅牢性）
void test_out_of_range_normalized() {
    const int n = voice_option_count();
    TEST_ASSERT_EQUAL_INT(voice_speaker_at(0), voice_speaker_at(n));      // n ≡ 0
    TEST_ASSERT_EQUAL_INT(voice_speaker_at(n - 1), voice_speaker_at(-1)); // -1 ≡ n-1
    TEST_ASSERT_NOT_NULL(voice_name_at(-100));
    TEST_ASSERT_NOT_NULL(voice_name_at(1000));
}

// 全候補の speaker id は非負、表示名は非空（契約の健全性）
void test_all_options_valid() {
    for (int i = 0; i < voice_option_count(); ++i) {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, voice_speaker_at(i));
        TEST_ASSERT_TRUE(std::strlen(voice_name_at(i)) > 0);
    }
}

// タップ左右判定：右半分=次へ、左半分=前へ（境界は右扱い）
void test_is_next_tap() {
    TEST_ASSERT_TRUE(voice_is_next_tap(200, 320));   // 右
    TEST_ASSERT_FALSE(voice_is_next_tap(50, 320));    // 左
    TEST_ASSERT_TRUE(voice_is_next_tap(160, 320));    // 中央=右扱い
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_default_is_zundamon);
    RUN_TEST(test_next_cycles);
    RUN_TEST(test_prev_cycles);
    RUN_TEST(test_out_of_range_normalized);
    RUN_TEST(test_all_options_valid);
    RUN_TEST(test_is_next_tap);
    return UNITY_END();
}
