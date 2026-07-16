#include <unity.h>
#include "video.h"

void setUp(void) {}
void tearDown(void) {}

// 開始直後は先頭フレーム（0）
void test_starts_at_first_frame() {
    TEST_ASSERT_EQUAL_INT(0, video_frame_at(0, 30, 100));
}

// 経過時間×fps でフレームが進む（10fps なら 100ms ごとに1フレーム）
void test_advances_with_time() {
    TEST_ASSERT_EQUAL_INT(0, video_frame_at(99, 10, 100));   // 0.99フレーム目 → 0
    TEST_ASSERT_EQUAL_INT(1, video_frame_at(100, 10, 100));  // ちょうど1フレーム
    TEST_ASSERT_EQUAL_INT(5, video_frame_at(550, 10, 100));  // 5.5 → 5（切り捨て）
}

// 末尾まで来たら先頭へループ（10fps・4フレーム → 1周=400ms）
void test_loops_at_end() {
    TEST_ASSERT_EQUAL_INT(0, video_frame_at(400, 10, 4));
    TEST_ASSERT_EQUAL_INT(1, video_frame_at(500, 10, 4));
}

// fps<=0 / frame_count<=0 は安全値 0
void test_invalid_args_are_zero() {
    TEST_ASSERT_EQUAL_INT(0, video_frame_at(1000, 0, 10));
    TEST_ASSERT_EQUAL_INT(0, video_frame_at(1000, -1, 10));
    TEST_ASSERT_EQUAL_INT(0, video_frame_at(1000, 30, 0));
    TEST_ASSERT_EQUAL_INT(0, video_frame_at(1000, 30, -5));
}

// 単一フレームなら常に0
void test_single_frame_stays() {
    TEST_ASSERT_EQUAL_INT(0, video_frame_at(12345, 30, 1));
}

// 長時間再生でも桁あふれしない（elapsed×fps が 32bit を超える領域）。
// 200,000,000ms(≈55時間)×30fps = 6,000,000,000（uint32 上限 ≈4.29e9 超）→ /1000=6,000,000 → %100=0。
void test_no_overflow_long_playback() {
    TEST_ASSERT_EQUAL_INT(0, video_frame_at(200000000u, 30, 100));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_at_first_frame);
    RUN_TEST(test_advances_with_time);
    RUN_TEST(test_loops_at_end);
    RUN_TEST(test_invalid_args_are_zero);
    RUN_TEST(test_single_frame_stays);
    RUN_TEST(test_no_overflow_long_playback);
    return UNITY_END();
}
