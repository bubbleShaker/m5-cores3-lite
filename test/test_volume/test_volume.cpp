#include <unity.h>
#include "volume.h"

// volume（純粋ロジック）の単体テスト。実機なしで増減のクランプ・
// setVolume 値への変換・左右タップ判定を検証する（Issue #70）。

void setUp(void) {}
void tearDown(void) {}

// 上げは1段ずつ増え、上限 kVolumeMax で頭打ち
void test_up_increments_and_clamps_at_max() {
    TEST_ASSERT_EQUAL_INT(1, volume_up(0));
    TEST_ASSERT_EQUAL_INT(kVolumeMax, volume_up(kVolumeMax - 1));
    TEST_ASSERT_EQUAL_INT(kVolumeMax, volume_up(kVolumeMax));  // 上限で止まる
}

// 下げは1段ずつ減り、下限 0 で頭打ち
void test_down_decrements_and_clamps_at_zero() {
    TEST_ASSERT_EQUAL_INT(kVolumeMax - 1, volume_down(kVolumeMax));
    TEST_ASSERT_EQUAL_INT(0, volume_down(1));
    TEST_ASSERT_EQUAL_INT(0, volume_down(0));  // 下限で止まる
}

// setVolume 値への変換：両端と初期値の代表点
void test_to_speaker_maps_range() {
    TEST_ASSERT_EQUAL_UINT8(0, volume_to_speaker(0));            // 無音
    TEST_ASSERT_EQUAL_UINT8(255, volume_to_speaker(kVolumeMax)); // 最大
    TEST_ASSERT_EQUAL_UINT8(182, volume_to_speaker(kVolumeDefault)); // 初期(≒従来180)
}

// 変換は単調増加（レベルが上がれば setVolume 値も下がらない）
void test_to_speaker_is_monotonic() {
    for (int lv = 1; lv <= kVolumeMax; ++lv) {
        TEST_ASSERT_TRUE(volume_to_speaker(lv) >= volume_to_speaker(lv - 1));
    }
}

// 範囲外レベルは内部クランプされる（変換で破綻しない）
void test_to_speaker_clamps_out_of_range() {
    TEST_ASSERT_EQUAL_UINT8(0, volume_to_speaker(-3));
    TEST_ASSERT_EQUAL_UINT8(255, volume_to_speaker(kVolumeMax + 10));
}

// 左右タップ判定：右半分=アップ、左半分=ダウン、中央は右扱い
void test_is_up_tap_splits_left_right() {
    const int w = 320;
    TEST_ASSERT_FALSE(volume_is_up_tap(0, w));        // 左端=ダウン
    TEST_ASSERT_FALSE(volume_is_up_tap(159, w));      // 中央直前=ダウン
    TEST_ASSERT_TRUE(volume_is_up_tap(160, w));       // 中央=アップ（境界は右）
    TEST_ASSERT_TRUE(volume_is_up_tap(319, w));       // 右端=アップ

    // 奇数幅でも整数除算(screenW/2)で破綻せず左右に二分できる（w=321→境界160）
    const int odd = 321;
    TEST_ASSERT_FALSE(volume_is_up_tap(159, odd));
    TEST_ASSERT_TRUE(volume_is_up_tap(160, odd));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_up_increments_and_clamps_at_max);
    RUN_TEST(test_down_decrements_and_clamps_at_zero);
    RUN_TEST(test_to_speaker_maps_range);
    RUN_TEST(test_to_speaker_is_monotonic);
    RUN_TEST(test_to_speaker_clamps_out_of_range);
    RUN_TEST(test_is_up_tap_splits_left_right);
    return UNITY_END();
}
