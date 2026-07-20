#include <unity.h>
#include <string.h>
#include "video.h"

void setUp(void) {}
void tearDown(void) {}

// フレームパス組み立て（0基点の index → 1基点・5桁ゼロ埋めの frame_%05d.jpg）。
// 端末では video_frame_at が返す 0..frames-1 をそのまま渡す想定。
void test_frame_path_basic() {
    char buf[64];
    TEST_ASSERT_TRUE(video_frame_path(buf, sizeof(buf), "/video/sample", 0));
    TEST_ASSERT_EQUAL_STRING("/video/sample/frame_00001.jpg", buf);
}

// index が進むと 1 加算された 5 桁になる
void test_frame_path_advances() {
    char buf[64];
    TEST_ASSERT_TRUE(video_frame_path(buf, sizeof(buf), "/video/sample", 41));
    TEST_ASSERT_EQUAL_STRING("/video/sample/frame_00042.jpg", buf);
}

// 5 桁を超える大きな番号でもゼロ埋めは崩れず素の桁数で出る
void test_frame_path_large_index() {
    char buf[64];
    TEST_ASSERT_TRUE(video_frame_path(buf, sizeof(buf), "/video/sample", 99999));
    TEST_ASSERT_EQUAL_STRING("/video/sample/frame_100000.jpg", buf);
}

// バッファが足りなければ false（切り詰めた文字列で drawJpgFile を呼ばせない）
void test_frame_path_buffer_too_small() {
    char buf[8];
    TEST_ASSERT_FALSE(video_frame_path(buf, sizeof(buf), "/video/sample", 0));
}

// 負の index は契約違反として false（frame_00000/frame_-0001 を作らせない）
void test_frame_path_negative_index() {
    char buf[64];
    TEST_ASSERT_FALSE(video_frame_path(buf, sizeof(buf), "/video/sample", -1));
}

// dir/buf が null なら false（防御）
void test_frame_path_null_args() {
    char buf[64];
    TEST_ASSERT_FALSE(video_frame_path(nullptr, sizeof(buf), "/video/sample", 0));
    TEST_ASSERT_FALSE(video_frame_path(buf, sizeof(buf), nullptr, 0));
}

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

// --- video_cycle_at（周回番号・#164） ---

// 1周目は 0。末尾フレームに居る間もまだ 0（境界の食い違いが無いこと）。
void test_cycle_starts_at_zero() {
    TEST_ASSERT_EQUAL_UINT32(0, video_cycle_at(0, 10, 100));
    TEST_ASSERT_EQUAL_UINT32(0, video_cycle_at(9900, 10, 100));  // 100枚目(index 99)＝まだ1周目
}

// 一周ちょうどで 1 になる。video_frame_at が 0 に戻る瞬間と一致すること。
void test_cycle_increments_at_wrap() {
    TEST_ASSERT_EQUAL_INT(0, video_frame_at(10000, 10, 100));    // 番号は先頭へ戻り
    TEST_ASSERT_EQUAL_UINT32(1, video_cycle_at(10000, 10, 100)); // 周回番号は繰り上がる
}

// 本命（reviewer 指摘）: 更新が飛んで一周ぶん以上進んでも取りこぼさない。
// frames=3/fps=30 は一周 100ms。100ms と 300ms で標本を取ると、フレーム番号はどちらも 0 のまま
// 全く動かない（戻りもしない）のに、実際には 2 周ぶん進んでいる。
// 「番号が戻ったか」で周回を検知する実装はこの入力を取りこぼし、音を鳴らし直せなかった。
void test_cycle_detects_skipped_wraps() {
    TEST_ASSERT_EQUAL_INT(0, video_frame_at(100, 30, 3));
    TEST_ASSERT_EQUAL_INT(0, video_frame_at(300, 30, 3));        // 番号は動かない＝戻りを検知できない
    TEST_ASSERT_EQUAL_UINT32(1, video_cycle_at(100, 30, 3));
    TEST_ASSERT_EQUAL_UINT32(3, video_cycle_at(300, 30, 3));     // 周回番号なら2周ぶん進んだと分かる
}

// 再生不能な引数は 0（video_frame_at と同じ安全値）。ゼロ除算を踏まないこと。
void test_cycle_invalid_args_are_zero() {
    TEST_ASSERT_EQUAL_UINT32(0, video_cycle_at(1000, 0, 100));
    TEST_ASSERT_EQUAL_UINT32(0, video_cycle_at(1000, -1, 100));
    TEST_ASSERT_EQUAL_UINT32(0, video_cycle_at(1000, 30, 0));
    TEST_ASSERT_EQUAL_UINT32(0, video_cycle_at(1000, 30, -5));
}

// 長時間再生でも桁あふれしない（video_frame_at と同じ領域を商側でも踏む）。
// 200,000,000ms×30fps/1000 = 6,000,000 → /100 = 60,000 周。
void test_cycle_no_overflow_long_playback() {
    TEST_ASSERT_EQUAL_UINT32(60000, video_cycle_at(200000000u, 30, 100));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_at_first_frame);
    RUN_TEST(test_advances_with_time);
    RUN_TEST(test_loops_at_end);
    RUN_TEST(test_invalid_args_are_zero);
    RUN_TEST(test_single_frame_stays);
    RUN_TEST(test_no_overflow_long_playback);
    RUN_TEST(test_frame_path_basic);
    RUN_TEST(test_frame_path_advances);
    RUN_TEST(test_frame_path_large_index);
    RUN_TEST(test_frame_path_buffer_too_small);
    RUN_TEST(test_frame_path_negative_index);
    RUN_TEST(test_frame_path_null_args);
    RUN_TEST(test_cycle_starts_at_zero);
    RUN_TEST(test_cycle_increments_at_wrap);
    RUN_TEST(test_cycle_detects_skipped_wraps);
    RUN_TEST(test_cycle_invalid_args_are_zero);
    RUN_TEST(test_cycle_no_overflow_long_playback);
    return UNITY_END();
}
