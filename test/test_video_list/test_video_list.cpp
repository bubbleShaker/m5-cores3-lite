#include <unity.h>
#include <cstring>
#include <cstdio>
#include "video_list.h"

void setUp(void) {}
void tearDown(void) {}

// ───────── 名前の妥当性判定（外部入力の検証・#175） ─────────

// ふつうの名前は通る
void test_name_valid_ok() {
    TEST_ASSERT_TRUE(video_name_valid("sample"));
    TEST_ASSERT_TRUE(video_name_valid("lain"));
    TEST_ASSERT_TRUE(video_name_valid("video_01"));
}

// null / 空 / "." は弾く
void test_name_valid_empty() {
    TEST_ASSERT_FALSE(video_name_valid(nullptr));
    TEST_ASSERT_FALSE(video_name_valid(""));
    TEST_ASSERT_FALSE(video_name_valid("."));
}

// 区切り文字・".." は /video/ の外へ抜けうるので弾く
void test_name_valid_separators() {
    TEST_ASSERT_FALSE(video_name_valid(".."));
    TEST_ASSERT_FALSE(video_name_valid("../secret"));
    TEST_ASSERT_FALSE(video_name_valid("a/b"));
    TEST_ASSERT_FALSE(video_name_valid("a\\b"));
    TEST_ASSERT_FALSE(video_name_valid("a..b"));  // ".." を含めば弾く（読む側と同じ規則）
}

// 長すぎる名前は弾き、上限ちょうどは通す（境界）
void test_name_valid_length() {
    char just_ok[kVideoNameMax + 1];
    std::memset(just_ok, 'a', kVideoNameMax);
    just_ok[kVideoNameMax] = '\0';
    TEST_ASSERT_TRUE(video_name_valid(just_ok));  // ちょうど上限

    char too_long[kVideoNameMax + 2];
    std::memset(too_long, 'a', kVideoNameMax + 1);
    too_long[kVideoNameMax + 1] = '\0';
    TEST_ASSERT_FALSE(video_name_valid(too_long));  // 1文字超過
}

// ───────── パス組み立て（#175） ─────────

// 妥当な名前から "/video/<name>" を作る
void test_build_dir_ok() {
    char buf[64];
    TEST_ASSERT_TRUE(video_build_dir(buf, sizeof(buf), "lain"));
    TEST_ASSERT_EQUAL_STRING("/video/lain", buf);
}

// 不正な名前では false を返し buf は空（中途半端なパスを使わせない）
void test_build_dir_rejects_bad_name() {
    char buf[64];
    TEST_ASSERT_FALSE(video_build_dir(buf, sizeof(buf), "../etc"));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

// buf が小さくて収まらないと false（切り詰めない）
void test_build_dir_truncation() {
    char buf[8];  // "/video/lain" は入らない
    TEST_ASSERT_FALSE(video_build_dir(buf, sizeof(buf), "lain"));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

// 上限ちょうどの名前でも、実機相当の g_videoDir[64] サイズなら "/video/<31文字>" が収まる（境界回帰）。
// kVideoNameMax を増やして端末バッファを溢れさせる変更が入ったら、ここで気付けるようにする。
void test_build_dir_max_name_fits_device_buf() {
    char name[kVideoNameMax + 1];
    std::memset(name, 'a', kVideoNameMax);
    name[kVideoNameMax] = '\0';
    char buf[64];  // main.cpp の g_videoDir[64] と同じ幅
    TEST_ASSERT_TRUE(video_build_dir(buf, sizeof(buf), name));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(std::strlen("/video/") + kVideoNameMax),
                          static_cast<int>(std::strlen(buf)));
}

// ───────── 候補リストの保持と選択（#175） ─────────

// clear 直後は 0 件、範囲外参照は nullptr（0件時の表示分岐の前提）
void test_list_empty() {
    VideoList list;
    video_list_clear(&list);
    TEST_ASSERT_EQUAL_INT(0, list.count);
    TEST_ASSERT_NULL(video_list_name_at(&list, 0));
    TEST_ASSERT_NULL(video_list_name_at(&list, -1));
}

// 妥当な名前だけ詰まり、名前が引ける
void test_list_add_and_get() {
    VideoList list;
    video_list_clear(&list);
    TEST_ASSERT_TRUE(video_list_add(&list, "sample"));
    TEST_ASSERT_TRUE(video_list_add(&list, "lain"));
    TEST_ASSERT_FALSE(video_list_add(&list, "../evil"));  // 不正は追加されない
    TEST_ASSERT_EQUAL_INT(2, list.count);
    TEST_ASSERT_EQUAL_STRING("sample", video_list_name_at(&list, 0));
    TEST_ASSERT_EQUAL_STRING("lain", video_list_name_at(&list, 1));
    TEST_ASSERT_NULL(video_list_name_at(&list, 2));
}

// 満杯を超える追加は false（溢れは捨てる・件数は上限で頭打ち）
void test_list_overflow() {
    VideoList list;
    video_list_clear(&list);
    char name[16];
    for (int i = 0; i < kVideoListCap; ++i) {
        std::snprintf(name, sizeof(name), "v%d", i);
        TEST_ASSERT_TRUE(video_list_add(&list, name));
    }
    TEST_ASSERT_EQUAL_INT(kVideoListCap, list.count);
    TEST_ASSERT_FALSE(video_list_add(&list, "overflow"));  // 満杯
    TEST_ASSERT_EQUAL_INT(kVideoListCap, list.count);      // 増えない
}

// ───────── タップ判定・カーソル巡回（#175） ─────────

// 右半分=決定、左半分=移動（境界は右扱い）
void test_is_decide_tap() {
    TEST_ASSERT_TRUE(video_is_decide_tap(200, 320));   // 右
    TEST_ASSERT_FALSE(video_is_decide_tap(50, 320));   // 左
    TEST_ASSERT_TRUE(video_is_decide_tap(160, 320));   // 中央=右扱い
}

// next は1つ進み、末尾の次は先頭へ巡回する
void test_list_next_cycles() {
    TEST_ASSERT_EQUAL_INT(1, video_list_next(0, 3));
    TEST_ASSERT_EQUAL_INT(2, video_list_next(1, 3));
    TEST_ASSERT_EQUAL_INT(0, video_list_next(2, 3));  // 末尾→先頭
}

// count<=0 は 0、範囲外インデックスも巡回で正規化して落ちない（堅牢性）
void test_list_next_robust() {
    TEST_ASSERT_EQUAL_INT(0, video_list_next(0, 0));
    TEST_ASSERT_EQUAL_INT(0, video_list_next(5, 0));
    TEST_ASSERT_EQUAL_INT(0, video_list_next(2, 3));   // 2 は範囲内→次は 0
    TEST_ASSERT_EQUAL_INT(1, video_list_next(3, 3));   // 3≡0 →次は 1
    TEST_ASSERT_EQUAL_INT(0, video_list_next(-1, 3));  // -1≡2 →次は 0
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_name_valid_ok);
    RUN_TEST(test_name_valid_empty);
    RUN_TEST(test_name_valid_separators);
    RUN_TEST(test_name_valid_length);
    RUN_TEST(test_build_dir_ok);
    RUN_TEST(test_build_dir_rejects_bad_name);
    RUN_TEST(test_build_dir_truncation);
    RUN_TEST(test_build_dir_max_name_fits_device_buf);
    RUN_TEST(test_list_empty);
    RUN_TEST(test_list_add_and_get);
    RUN_TEST(test_list_overflow);
    RUN_TEST(test_is_decide_tap);
    RUN_TEST(test_list_next_cycles);
    RUN_TEST(test_list_next_robust);
    return UNITY_END();
}
