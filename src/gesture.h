#pragma once
#include <cstdint>

// タッチ・ジェスチャ検出（テーマ M / Issue #33 の純粋ロジック部）。
// 毎フレームの「今触れているか」の系列から「短タップ / 長押し」を判定する。
// millis() に依存させず now_ms を引数で受けることで、実機なしで単体テストできる。

// 長押しと判定する押下継続時間（ms）。これ以上押し続けると LongPress を発火する。
constexpr uint32_t kLongPressMs = 800;

// 認識したジェスチャ。touch_update が「認識した瞬間」に1回だけ返す。
enum class TouchEvent { None, Tap, LongPress };

// 押下状態を持ち越すための状態。呼び出し側が1個保持して毎フレーム渡す。
//   pressed        … 直前フレームで触れていたか（立ち上がり/立ち下がり検出用）
//   press_start_ms … 現在の押下が始まった時刻
//   long_fired     … この押下で LongPress を既に発火したか（二重発火防止）
struct TouchTracker {
    bool     pressed        = false;
    uint32_t press_start_ms = 0;
    bool     long_fired     = false;
};

// 毎フレーム1サンプルを流し、認識した瞬間にイベントを返す純粋関数。
//   LongPress … 押しっぱなしで継続時間が kLongPressMs に達した瞬間に1回だけ。
//   Tap       … kLongPressMs 未満で離した瞬間（＝短タップ）。
//   None      … それ以外。
// 長押しが発火した後の「離し」では Tap を出さない（二重発火防止）。
// 前提: 呼び出し側は十分な頻度（例: 約30fps）でポーリングする。
TouchEvent touch_update(TouchTracker& t, bool touching, uint32_t now_ms);
