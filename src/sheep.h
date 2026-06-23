#pragma once
#include <cstdint>

// 羊の上下揺れ（bob）アニメのタイミング定数（ハード非依存・テストからも参照する）。
//   kSheepBobPeriodMs    … 揺れ1往復の長さ（ゆっくり＝大きい値）
//   kSheepBobAmplitudePx … 揺れの片振幅（中心からの最大ずれpx）
constexpr uint32_t kSheepBobPeriodMs    = 2000;
constexpr int      kSheepBobAmplitudePx = 6;

// 経過時間(ms)から羊の上下オフセット(px)を返す純粋関数。
// millis() に依存させず引数で時間を受けることで、実機なしで単体テストできる。
//   -amplitude 〜 +amplitude を三角波で往復する（中心が 0）。
//   位相0で -amplitude（一番下）、半周期で +amplitude（一番上）。
//   上下どちらに使うかは描画側の解釈に委ねる（純粋に値だけ返す）。
int sheep_bob_offset(uint32_t elapsed_ms);
