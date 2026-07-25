#pragma once
#include <cstdint>

// タッチ・ジェスチャ検出（テーマ M / Issue #33 の純粋ロジック部）。
// 毎フレームの「今触れているか」の系列から「短タップ / 長押し / 左右スワイプ」を判定する。
// millis() に依存させず now_ms を引数で受けることで、実機なしで単体テストできる。

// 長押しと判定する押下継続時間（ms）。これ以上押し続けると LongPress を発火する。
constexpr uint32_t kLongPressMs = 800;

// スワイプと判定する横移動量（px）。離した瞬間の |X移動量| がこれ以上なら Tap ではなく
// Swipe とみなす（#193・曲選択カルーセル）。CoreS3 の画面幅 320px に対し、指の置き直しの
// ブレ（数px〜十数px）では発火せず、意図した払い操作（画面の1/6程度）では確実に届く値。
constexpr int kSwipeMinPx = 50;

// 認識したジェスチャ。touch_update が「認識した瞬間」に1回だけ返す。
// SwipeLeft は「指が左へ動いた」（dx が負）。カルーセルでは内容が左へ流れる＝次の曲。
enum class TouchEvent { None, Tap, LongPress, SwipeLeft, SwipeRight };

// 押下状態を持ち越すための状態。呼び出し側が1個保持して毎フレーム渡す。
//   pressed        … 直前フレームで触れていたか（立ち上がり/立ち下がり検出用）
//   press_start_ms … 現在の押下が始まった時刻
//   long_fired     … この押下で LongPress を既に発火したか（二重発火防止）
//   press_start_x  … 現在の押下が始まった X 座標（スワイプの移動量の基準・#193）
struct TouchTracker {
    bool     pressed        = false;
    uint32_t press_start_ms = 0;
    bool     long_fired     = false;
    int      press_start_x  = 0;
};

// 毎フレーム1サンプルを流し、認識した瞬間にイベントを返す純粋関数。
//   LongPress  … 押しっぱなしで継続時間が kLongPressMs に達した瞬間に1回だけ。
//                ただし X 移動量が kSwipeMinPx 以上の間は発火しない（スワイプ操作の
//                途中で長押し＝メニュー復帰が誤爆しないように・#193）。
//   SwipeLeft  … 離した瞬間、X 移動量 <= -kSwipeMinPx（指が左へ払われた）。
//   SwipeRight … 離した瞬間、X 移動量 >= +kSwipeMinPx（指が右へ払われた）。
//   Tap        … kLongPressMs 未満・移動量 kSwipeMinPx 未満で離した瞬間（＝短タップ）。
//   None       … それ以外。
// 長押しが発火した後の「離し」では Tap も Swipe も出さない（二重発火防止）。
// x は現在のタッチ X 座標。触れていないフレームでは無視される（直前値のままでよい）。
// デフォルト引数 0 なのは、座標を使わない既存呼び出し（テスト含む）が移動量 0 ＝
// 従来どおり Tap/LongPress のみの挙動になるため（後方互換）。
// 前提: 呼び出し側は十分な頻度（例: 約30fps）でポーリングする。
TouchEvent touch_update(TouchTracker& t, bool touching, uint32_t now_ms, int x = 0);
