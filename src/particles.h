#pragma once
#include <cstdint>

// 発話中に背景へ出す「円形の素粒子エフェクト」の幾何を作る純粋ロジック（Issue #109）。
// 中心 (cx,cy) の周りに count 個の粒子をリング状に配置し、時間 t（秒）で公転させ、
// intensity(0..1) でリング半径の脈動と粒の明るさ・大きさを変える。描画は呼び出し側（実機）。
// millis()・M5 に依存せず、同じ引数なら必ず同じ結果（native で決定論的にテストできる）。

struct Particle {
    int     x;       // 画面座標X（cx を中心にリング上へ配置）
    int     y;       // 画面座標Y
    uint8_t level;   // 明るさ 0..255（intensity と個体位相で決まる。0=消灯）
    uint8_t radius;  // 粒の半径(px)（intensity で少し太る）
};

// out[capacity] にリング状の粒子を書き込む。返り値＝実際に書いた粒子数（min(count, capacity)）。
//   baseRadius … 脈動していない時のリング半径(px)
//   t          … アニメ時間（秒・単調増加）。公転と波打ちの位相に使う。
//   intensity  … 0..1（範囲外は内部でクランプ）。0 なら明るさ 0＝実質見えない。
int particle_ring(Particle* out, int capacity, int count,
                  int cx, int cy, int baseRadius, float t, float intensity);
