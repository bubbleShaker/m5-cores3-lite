#pragma once
#include <cstdint>

// まばたきアニメーションのタイミング定数（ハード非依存・テストからも参照する）。
//   kBlinkIntervalMs … まばたき1周期の長さ（この周期の先頭でだけ瞬きする）
//   kBlinkDurationMs … 瞬き動作（開→閉→開）にかける時間
constexpr uint32_t kBlinkIntervalMs = 3000;
constexpr uint32_t kBlinkDurationMs = 150;

// 経過時間(ms)から目の開き具合を 0.0(完全に閉じ)〜1.0(完全に開き) で返す純粋関数。
// millis() に依存させず引数で時間を受けることで、実機なしで単体テストできる。
//   周期の先頭 kBlinkDurationMs だけ「開→閉→開」の三角波、残りは 1.0 で開いたまま。
float eye_openness(uint32_t elapsed_ms);
