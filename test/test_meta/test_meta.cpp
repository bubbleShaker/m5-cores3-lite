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

// ───────── 文字列値の取り出し（Issue #170 のパック方式 `pack=frames.bin`） ─────────
void test_str_reads_value() {
    const char* m = "fps=10\nframes=2355\npack=frames.bin\n";
    char buf[32];
    TEST_ASSERT_TRUE(meta_get_str(m, "pack", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("frames.bin", buf);
}

// 最終行に改行が無くても読める
void test_str_last_line_without_newline() {
    const char* m = "fps=10\npack=frames.bin";
    char buf[32];
    TEST_ASSERT_TRUE(meta_get_str(m, "pack", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("frames.bin", buf);
}

// CRLF の meta.txt でも '\r' を値に含めない（"frames.bin\r" を開こうとしない）
void test_str_crlf_not_included() {
    const char* m = "fps=10\r\npack=frames.bin\r\n";
    char buf[32];
    TEST_ASSERT_TRUE(meta_get_str(m, "pack", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("frames.bin", buf);
}

// キーが無ければ false かつ buf は空（旧アセット＝従来の連番方式に落ちる経路）
void test_str_missing_key_is_empty() {
    const char* m = "fps=10\nframes=300\n";
    char buf[32] = "dirty";
    TEST_ASSERT_FALSE(meta_get_str(m, "pack", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

// 部分一致しない（meta_get_int と同じ規則）
void test_str_no_substring_false_match() {
    const char* m = "unpack=other.bin\n";
    char buf[32];
    TEST_ASSERT_FALSE(meta_get_str(m, "pack", buf, sizeof(buf)));
}

// buf に収まらなければ false かつ空。切り詰めたファイル名で SD.open を呼ばせない
void test_str_truncation_rejected() {
    const char* m = "pack=frames.bin\n";
    char buf[5];
    TEST_ASSERT_FALSE(meta_get_str(m, "pack", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

// "key=" だけの行は値なし
void test_str_empty_value_is_false() {
    const char* m = "pack=\nfps=10\n";
    char buf[32];
    TEST_ASSERT_FALSE(meta_get_str(m, "pack", buf, sizeof(buf)));
}

// null・サイズ0 の防御
void test_str_null_and_empty_safe() {
    char buf[32];
    TEST_ASSERT_FALSE(meta_get_str(nullptr, "pack", buf, sizeof(buf)));
    TEST_ASSERT_FALSE(meta_get_str("pack=x\n", nullptr, buf, sizeof(buf)));
    TEST_ASSERT_FALSE(meta_get_str("pack=x\n", "pack", nullptr, 32));
    TEST_ASSERT_FALSE(meta_get_str("pack=x\n", "pack", buf, 0));
}

// キーの「存在」は値が取り出せるかとは別（reviewer 指摘・#170）。
// pack=<32文字以上> や pack= を「宣言が無い」と読むと、遅い連番方式へ黙って落ちてしまう。
void test_has_key_distinguishes_presence_from_value() {
    TEST_ASSERT_TRUE(meta_has_key("fps=10\npack=frames.bin\n", "pack"));
    TEST_ASSERT_TRUE(meta_has_key("pack=\n", "pack"));            // 値が空でも「宣言はある」
    TEST_ASSERT_FALSE(meta_has_key("fps=10\nframes=300\n", "pack"));
    TEST_ASSERT_FALSE(meta_has_key("unpack=x\n", "pack"));        // 部分一致しない
    TEST_ASSERT_FALSE(meta_has_key(nullptr, "pack"));
    TEST_ASSERT_FALSE(meta_has_key("pack=x\n", nullptr));
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
    RUN_TEST(test_str_reads_value);
    RUN_TEST(test_str_last_line_without_newline);
    RUN_TEST(test_str_crlf_not_included);
    RUN_TEST(test_str_missing_key_is_empty);
    RUN_TEST(test_str_no_substring_false_match);
    RUN_TEST(test_str_truncation_rejected);
    RUN_TEST(test_str_empty_value_is_false);
    RUN_TEST(test_str_null_and_empty_safe);
    RUN_TEST(test_has_key_distinguishes_presence_from_value);
    return UNITY_END();
}
