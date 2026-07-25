#include "fractal.h"

uint8_t fractal_value(int x, int y, uint32_t t_ms) {
    // 座標を時間でドリフトさせる。x と y で速度を変える（同速だと単なる対角線方向の
    // 平行移動に見えるため）。下位 16bit へのマスクは int 変換を溢れさせないための保険で、
    // 模様には影響しない（最終出力 8bit に効くのは dx/dy の下位 10bit だけなので、
    // マスクの折り返しは自然なビット繰り上がりと区別が付かない）。
    const int dx = x + static_cast<int>((t_ms / 40u) & 0xFFFFu);   // 25px/s
    const int dy = y + static_cast<int>((t_ms / 56u) & 0xFFFFu);   // 約18px/s
    // 細かい層＋1/4 縮尺の粗い層。どちらも XOR なので t=0 では x/y 対称（テストで固定）。
    const int fine   = dx ^ dy;
    const int coarse = (dx >> 2) ^ (dy >> 2);
    // パレット位相 t/32: 約 8.2 秒で 256 段を一巡する（速すぎるとチカチカする・遅すぎると
    // 静止画に見える、の間を取った初期値。実機の見た目で調整してよい）。
    const uint32_t phase = (t_ms / 32u) & 0xFFu;
    return static_cast<uint8_t>((static_cast<uint32_t>(fine + coarse) + phase) & 0xFFu);
}

uint32_t fractal_shade(uint32_t tone, uint8_t v) {
    const uint32_t r = ((tone >> 16) & 0xFFu) * v / 255u;
    const uint32_t g = ((tone >> 8) & 0xFFu) * v / 255u;
    const uint32_t b = (tone & 0xFFu) * v / 255u;
    return (r << 16) | (g << 8) | b;
}
