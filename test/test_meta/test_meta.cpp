#include <unity.h>
#include "meta.h"

void setUp(void) {}
void tearDown(void) {}

// tools/video2frames.py が実際に出す meta.txt と同じ形（末尾に改行あり）。
static const char* kMeta =
    "fps=8\n"
    "frames=1234\n"
    "width=320\n"
    "height=240\n"
    "sample_rate=16000\n"
    "channels=1\n";

// 各キーが正しい数値で取れる
void test_reads_each_key() {
    TEST_ASSERT_EQUAL_INT(8,     meta_get_int(kMeta, "fps", -1));
    TEST_ASSERT_EQUAL_INT(1234,  meta_get_int(kMeta, "frames", -1));
    TEST_ASSERT_EQUAL_INT(320,   meta_get_int(kMeta, "width", -1));
    TEST_ASSERT_EQUAL_INT(240,   meta_get_int(kMeta, "height", -1));
    TEST_ASSERT_EQUAL_INT(16000, meta_get_int(kMeta, "sample_rate", -1));
    TEST_ASSERT_EQUAL_INT(1,     meta_get_int(kMeta, "channels", -1));
}

// 無いキーは fallback
void test_missing_key_returns_fallback() {
    TEST_ASSERT_EQUAL_INT(-99, meta_get_int(kMeta, "bitrate", -99));
}

// 部分一致で誤爆しない：
//   "rate" は "sample_rate" の一部だが行頭一致でないので当たらない。
//   "channel" は "channels" と '=' 直前まで一致するが末尾 '=' が合わず当たらない。
void test_no_substring_false_match() {
    TEST_ASSERT_EQUAL_INT(-1, meta_get_int(kMeta, "rate", -1));
    TEST_ASSERT_EQUAL_INT(-1, meta_get_int(kMeta, "channel", -1));
}

// 末尾に改行が無い最終行でも読める（堅牢性）
void test_last_line_without_newline() {
    const char* m = "fps=12\nframes=42";  // 末尾改行なし
    TEST_ASSERT_EQUAL_INT(12, meta_get_int(m, "fps", -1));
    TEST_ASSERT_EQUAL_INT(42, meta_get_int(m, "frames", -1));
}

// null / 空キーは fallback（クラッシュしない）
void test_null_and_empty_safe() {
    TEST_ASSERT_EQUAL_INT(7, meta_get_int(nullptr, "fps", 7));
    TEST_ASSERT_EQUAL_INT(7, meta_get_int(kMeta, nullptr, 7));
    TEST_ASSERT_EQUAL_INT(7, meta_get_int(kMeta, "", 7));
}

// CRLF（\r\n）混入でも読める。Windows で meta.txt を生成/コピーした場合に \r が入り得るが、
// atoi は数字の後の '\r' で自然に停止するので値は壊れない（SD/Windows 由来の改行への保証）。
void test_crlf_line_endings() {
    const char* m = "fps=8\r\nframes=1234\r\n";
    TEST_ASSERT_EQUAL_INT(8,    meta_get_int(m, "fps", -1));
    TEST_ASSERT_EQUAL_INT(1234, meta_get_int(m, "frames", -1));
}

// 値が空（"fps="）は atoi("") = 0 として返る（fallback ではなく 0。キー自体は在るため）。
void test_empty_value_is_zero() {
    const char* m = "fps=\nframes=10\n";
    TEST_ASSERT_EQUAL_INT(0,  meta_get_int(m, "fps", -1));
    TEST_ASSERT_EQUAL_INT(10, meta_get_int(m, "frames", -1));
}

// 同一キーが重複したら先に現れた行が勝つ（行頭から順に走査して最初の一致を返す）。
void test_duplicate_key_first_wins() {
    const char* m = "fps=8\nfps=99\n";
    TEST_ASSERT_EQUAL_INT(8, meta_get_int(m, "fps", -1));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_reads_each_key);
    RUN_TEST(test_missing_key_returns_fallback);
    RUN_TEST(test_no_substring_false_match);
    RUN_TEST(test_last_line_without_newline);
    RUN_TEST(test_null_and_empty_safe);
    RUN_TEST(test_crlf_line_endings);
    RUN_TEST(test_empty_value_is_zero);
    RUN_TEST(test_duplicate_key_first_wins);
    return UNITY_END();
}
