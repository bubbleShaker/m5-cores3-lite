#include <unity.h>
#include "gesture.h"

void setUp(void) {}
void tearDown(void) {}

// 触れていない間はイベント無し
void test_idle_returns_none() {
    TouchTracker t;
    TEST_ASSERT_TRUE(touch_update(t, false, 0) == TouchEvent::None);
    TEST_ASSERT_TRUE(touch_update(t, false, 100) == TouchEvent::None);
}

// 押し始め（立ち上がり）単体ではまだイベント無し
void test_press_start_is_none() {
    TouchTracker t;
    TEST_ASSERT_TRUE(touch_update(t, true, 0) == TouchEvent::None);
}

// 閾値未満で離したら Tap
void test_short_press_emits_tap_on_release() {
    TouchTracker t;
    touch_update(t, true, 0);                              // 押下開始
    TEST_ASSERT_TRUE(touch_update(t, true, 100) == TouchEvent::None);   // 保持
    TEST_ASSERT_TRUE(touch_update(t, false, 200) == TouchEvent::Tap);   // 短時間で離す
}

// 押しっぱなしで閾値に達した瞬間に LongPress を1回だけ
void test_long_press_fires_once_at_threshold() {
    TouchTracker t;
    touch_update(t, true, 0);                                              // 押下開始
    TEST_ASSERT_TRUE(touch_update(t, true, kLongPressMs - 1) == TouchEvent::None);
    TEST_ASSERT_TRUE(touch_update(t, true, kLongPressMs) == TouchEvent::LongPress);
    // それ以降の保持フレームでは再発火しない
    TEST_ASSERT_TRUE(touch_update(t, true, kLongPressMs + 100) == TouchEvent::None);
    TEST_ASSERT_TRUE(touch_update(t, true, kLongPressMs + 500) == TouchEvent::None);
}

// 長押し発火後に離しても Tap は出ない（二重発火防止）
void test_no_tap_after_long_press() {
    TouchTracker t;
    touch_update(t, true, 0);
    touch_update(t, true, kLongPressMs);                  // LongPress 発火
    TEST_ASSERT_TRUE(touch_update(t, false, kLongPressMs + 50) == TouchEvent::None);
}

// 連続した押下で状態がリセットされる（Tap → 別の Tap）
void test_consecutive_taps() {
    TouchTracker t;
    touch_update(t, true, 0);
    TEST_ASSERT_TRUE(touch_update(t, false, 100) == TouchEvent::Tap);
    touch_update(t, true, 500);
    TEST_ASSERT_TRUE(touch_update(t, false, 600) == TouchEvent::Tap);
}

// 長押し→離し→短タップ の順に正しく認識される
void test_long_then_tap() {
    TouchTracker t;
    touch_update(t, true, 0);
    TEST_ASSERT_TRUE(touch_update(t, true, kLongPressMs) == TouchEvent::LongPress);
    touch_update(t, false, kLongPressMs + 50);            // 離す（None）
    touch_update(t, true, 2000);                          // 次の押下開始
    TEST_ASSERT_TRUE(touch_update(t, false, 2100) == TouchEvent::Tap);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_returns_none);
    RUN_TEST(test_press_start_is_none);
    RUN_TEST(test_short_press_emits_tap_on_release);
    RUN_TEST(test_long_press_fires_once_at_threshold);
    RUN_TEST(test_no_tap_after_long_press);
    RUN_TEST(test_consecutive_taps);
    RUN_TEST(test_long_then_tap);
    return UNITY_END();
}
