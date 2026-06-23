#include "gesture.h"

TouchEvent touch_update(TouchTracker& t, bool touching, uint32_t now_ms) {
    TouchEvent ev = TouchEvent::None;

    if (touching) {
        if (!t.pressed) {
            // 立ち上がり: 押下開始。状態をリセットして計測を始める。
            t.pressed        = true;
            t.press_start_ms = now_ms;
            t.long_fired     = false;
        } else if (!t.long_fired && (now_ms - t.press_start_ms) >= kLongPressMs) {
            // 押下継続中に閾値到達: 長押しを1回だけ発火する。
            t.long_fired = true;
            ev = TouchEvent::LongPress;
        }
    } else {
        if (t.pressed) {
            // 立ち下がり: 離した。長押し未発火かつ短時間なら短タップ。
            if (!t.long_fired && (now_ms - t.press_start_ms) < kLongPressMs) {
                ev = TouchEvent::Tap;
            }
            t.pressed = false;
        }
    }

    return ev;
}
