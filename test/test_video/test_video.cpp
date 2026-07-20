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

// ───────── パック方式の索引読み取り（Issue #170） ─────────
// 索引部は「offset(uint32 LE), length(uint32 LE)」の 8 バイト固定長レコードの並び。
// テスト用に 3 フレームぶんの索引を組み立てる（データ部は 0..99 の 100 バイトを想定）。
//   #0 → offset 0,  length 10
//   #1 → offset 10, length 40
//   #2 → offset 50, length 50（データ部の末尾ちょうどまで）
static const uint8_t kIndex3[24] = {
    0x00, 0x00, 0x00, 0x00,  0x0A, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x00,  0x28, 0x00, 0x00, 0x00,
    0x32, 0x00, 0x00, 0x00,  0x32, 0x00, 0x00, 0x00,
};

void test_pack_entry_reads_first() {
    uint32_t off = 0xDEAD, len = 0xBEEF;
    TEST_ASSERT_TRUE(video_pack_entry(kIndex3, sizeof(kIndex3), 3, 0, 100, &off, &len));
    TEST_ASSERT_EQUAL_UINT32(0, off);
    TEST_ASSERT_EQUAL_UINT32(10, len);
}

// レコード幅 8 バイトぶん正しく飛ぶ（幅を間違えると隣のフィールドを読む）
void test_pack_entry_reads_middle() {
    uint32_t off = 0, len = 0;
    TEST_ASSERT_TRUE(video_pack_entry(kIndex3, sizeof(kIndex3), 3, 1, 100, &off, &len));
    TEST_ASSERT_EQUAL_UINT32(10, off);
    TEST_ASSERT_EQUAL_UINT32(40, len);
}

// データ部の末尾ちょうどに終わる entry は有効（off-by-one で弾かないこと）
void test_pack_entry_exact_end_is_valid() {
    uint32_t off = 0, len = 0;
    TEST_ASSERT_TRUE(video_pack_entry(kIndex3, sizeof(kIndex3), 3, 2, 100, &off, &len));
    TEST_ASSERT_EQUAL_UINT32(50, off);
    TEST_ASSERT_EQUAL_UINT32(50, len);
}

// リトルエンディアンで読めているか（バイト順を間違えると桁が入れ替わった巨大値になる）
void test_pack_entry_little_endian() {
    const uint8_t idx[8] = { 0x78, 0x56, 0x34, 0x12,  0x21, 0x43, 0x00, 0x00 };
    uint32_t off = 0, len = 0;
    TEST_ASSERT_TRUE(video_pack_entry(idx, sizeof(idx), 1, 0, 0x20000000u, &off, &len));
    TEST_ASSERT_EQUAL_UINT32(0x12345678u, off);
    TEST_ASSERT_EQUAL_UINT32(0x00004321u, len);
}

// 範囲外の番号（負値・frame_count 以上）は読まない
void test_pack_entry_out_of_range_index() {
    uint32_t off = 0, len = 0;
    TEST_ASSERT_FALSE(video_pack_entry(kIndex3, sizeof(kIndex3), 3, -1, 100, &off, &len));
    TEST_ASSERT_FALSE(video_pack_entry(kIndex3, sizeof(kIndex3), 3, 3, 100, &off, &len));
}

// meta.txt の frames に対して索引部が短い（frames.bin だけ古い等）なら全て false。
// ここを通すとデータ部の JPEG を索引として読むことになる。
void test_pack_entry_index_too_short() {
    uint32_t off = 0, len = 0;
    TEST_ASSERT_FALSE(video_pack_entry(kIndex3, 16, 3, 0, 100, &off, &len));
}

// 索引長が 8 の倍数でない（＝末尾のレコードが途中で切れている）時、その半端なぶんは
// レコードとして数えない。切り上げてしまうと索引の外を読む。
void test_pack_entry_partial_trailing_record() {
    uint32_t off = 0, len = 0;
    // 20 バイト = 2 レコード + 半端 4 バイト。3 枚を宣言していれば足りない
    TEST_ASSERT_FALSE(video_pack_entry(kIndex3, 20, 3, 0, 100, &off, &len));
    // 2 枚の宣言なら 2 レコードぶんは揃っているので読める
    TEST_ASSERT_TRUE(video_pack_entry(kIndex3, 20, 2, 1, 100, &off, &len));
    TEST_ASSERT_EQUAL_UINT32(10, off);
}

// データ部をはみ出す entry は false（壊れた索引で範囲外読みをしない）
void test_pack_entry_overruns_data() {
    const uint8_t idx[8] = { 0x32, 0x00, 0x00, 0x00,  0x33, 0x00, 0x00, 0x00 };  // off=50 len=51
    uint32_t off = 0, len = 0;
    TEST_ASSERT_FALSE(video_pack_entry(idx, sizeof(idx), 1, 0, 100, &off, &len));
}

// offset+length が uint32 を回り込む値でも範囲内と誤判定しない（64bit で足している）
void test_pack_entry_offset_length_overflow() {
    const uint8_t idx[8] = { 0xFF, 0xFF, 0xFF, 0xFF,  0x0A, 0x00, 0x00, 0x00 };
    uint32_t off = 0, len = 0;
    TEST_ASSERT_FALSE(video_pack_entry(idx, sizeof(idx), 1, 0, 100, &off, &len));
}

// length==0 は描けない。0 バイト read を drawJpg に渡させない
void test_pack_entry_zero_length() {
    const uint8_t idx[8] = { 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00 };
    uint32_t off = 0, len = 0;
    TEST_ASSERT_FALSE(video_pack_entry(idx, sizeof(idx), 1, 0, 100, &off, &len));
}

// null・frame_count<=0 の防御
void test_pack_entry_invalid_args() {
    uint32_t off = 0, len = 0;
    TEST_ASSERT_FALSE(video_pack_entry(nullptr, 24, 3, 0, 100, &off, &len));
    TEST_ASSERT_FALSE(video_pack_entry(kIndex3, sizeof(kIndex3), 0, 0, 100, &off, &len));
    TEST_ASSERT_FALSE(video_pack_entry(kIndex3, sizeof(kIndex3), 3, 0, 100, nullptr, &len));
    TEST_ASSERT_FALSE(video_pack_entry(kIndex3, sizeof(kIndex3), 3, 0, 100, &off, nullptr));
}

// 失敗時に出力を書き換えない（呼び出し側が戻り値を見落としても前回値のまま seek しない…
// ではなく、そもそも壊れた値が入らないことを保証する）
void test_pack_entry_failure_keeps_outputs() {
    uint32_t off = 0xAAAAAAAAu, len = 0xBBBBBBBBu;
    TEST_ASSERT_FALSE(video_pack_entry(kIndex3, sizeof(kIndex3), 3, 99, 100, &off, &len));
    TEST_ASSERT_EQUAL_UINT32(0xAAAAAAAAu, off);
    TEST_ASSERT_EQUAL_UINT32(0xBBBBBBBBu, len);
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
    RUN_TEST(test_pack_entry_reads_first);
    RUN_TEST(test_pack_entry_reads_middle);
    RUN_TEST(test_pack_entry_exact_end_is_valid);
    RUN_TEST(test_pack_entry_little_endian);
    RUN_TEST(test_pack_entry_out_of_range_index);
    RUN_TEST(test_pack_entry_index_too_short);
    RUN_TEST(test_pack_entry_partial_trailing_record);
    RUN_TEST(test_pack_entry_overruns_data);
    RUN_TEST(test_pack_entry_offset_length_overflow);
    RUN_TEST(test_pack_entry_zero_length);
    RUN_TEST(test_pack_entry_invalid_args);
    RUN_TEST(test_pack_entry_failure_keeps_outputs);
    return UNITY_END();
}
