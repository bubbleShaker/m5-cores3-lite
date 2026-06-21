#pragma once
#include <cstdint>

// まばたきアニメーションのタイミング定数（ハード非依存・テストからも参照する）。
//   kBlinkIntervalMs … まばたき1周期の長さ（この周期の先頭でだけ瞬きする）
//   kBlinkDurationMs … 瞬き動作（開→閉→開）にかける時間
constexpr uint32_t kBlinkIntervalMs = 3000;
constexpr uint32_t kBlinkDurationMs = 150;

// 口パク1周期の長さ（短い＝速く開閉する。まばたきより速い 200ms ≒ 5Hz）。
constexpr uint32_t kMouthCycleMs = 200;

// 経過時間(ms)から目の開き具合を 0.0(完全に閉じ)〜1.0(完全に開き) で返す純粋関数。
// millis() に依存させず引数で時間を受けることで、実機なしで単体テストできる。
//   周期の先頭 kBlinkDurationMs だけ「開→閉→開」の三角波、残りは 1.0 で開いたまま。
float eye_openness(uint32_t elapsed_ms);

// 経過時間(ms)と「喋っているか」から口の開き具合を 0.0(閉)〜1.0(開) で返す純粋関数。
//   speaking == false → 常に 0.0（閉じたまま）
//   speaking == true  → kMouthCycleMs 周期の「閉→開→閉」三角波で開閉
// speaking を引数にすることで、後段の対話レイヤーが応答中だけ true を渡せる。
float mouth_openness(uint32_t elapsed_ms, bool speaking);
