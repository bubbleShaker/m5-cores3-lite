#include "gesture.h"

// |a - b| を int で（cstdlib の abs に頼らず自前で。引数が int なので溢れない）
static int abs_dx(int a, int b) {
    const int d = a - b;
    return d < 0 ? -d : d;
}

TouchEvent touch_update(TouchTracker& t, bool touching, uint32_t now_ms, int x) {
    TouchEvent ev = TouchEvent::None;

    if (touching) {
        if (!t.pressed) {
            // 立ち上がり: 押下開始。状態をリセットして計測を始める。
            t.pressed        = true;
            t.press_start_ms = now_ms;
            t.long_fired     = false;
            t.press_start_x  = x;
            t.moved          = false;
        } else {
            // 一度でも大きく動いたらラッチする（離す瞬間に始点付近へ戻っていても
            // 「払ってから戻した」操作を Tap と誤認しないため・#193）。
            if (abs_dx(x, t.press_start_x) >= kSwipeMinPx) t.moved = true;
            if (!t.long_fired && (now_ms - t.press_start_ms) >= kLongPressMs &&
                abs_dx(x, t.press_start_x) < kSwipeMinPx) {
                // 押下継続中に閾値到達: 長押しを1回だけ発火する。
                // 横に大きく動いている「間」は発火させない（スワイプの途中で長押し＝メニュー
                // 復帰が誤爆すると、曲を選んでいたのに画面ごと飛ばされる・#193）。指を始点へ
                // 戻して押し続けるのは意図的な長押しとみなし、moved が立っていても発火させる。
                t.long_fired = true;
                ev = TouchEvent::LongPress;
            }
        }
    } else {
        if (t.pressed) {
            // 立ち下がり: 離した。長押し発火済みなら何も出さない（二重発火防止）。
            // それ以外は移動量を先に見る。スワイプは「素早く払う」とは限らず
            // kLongPressMs を超えてゆっくり払われることもあるため、時間では弾かない
            // （移動量が大きい時点で Tap と混同する余地は無い）。
            if (!t.long_fired) {
                const int dx = x - t.press_start_x;
                if (dx <= -kSwipeMinPx) {
                    ev = TouchEvent::SwipeLeft;
                } else if (dx >= kSwipeMinPx) {
                    ev = TouchEvent::SwipeRight;
                } else if (!t.moved && (now_ms - t.press_start_ms) < kLongPressMs) {
                    ev = TouchEvent::Tap;
                }
            }
            t.pressed = false;
        }
    }

    return ev;
}
