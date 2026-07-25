#include "fractal.h"

void fractal_offsets(uint32_t t_ms, int* out_dx, int* out_dy, uint32_t* out_phase) {
    if (out_dx == nullptr || out_dy == nullptr || out_phase == nullptr) return;
    // 座標を時間でドリフトさせる。x と y で速度を変える（同速だと単なる対角線方向の
    // 平行移動に見えるため）。下位 16bit へのマスクは int 変換を溢れさせないための保険で、
    // 模様には影響しない（最終出力 8bit に効くのは座標の下位 10bit だけなので、
    // マスクの折り返しは自然なビット繰り上がりと区別が付かない。この非自明な主張は
    // test_fractal の参照実装比較テストで固定してある）。
    *out_dx = static_cast<int>((t_ms / 40u) & 0xFFFFu);   // 25px/s
    *out_dy = static_cast<int>((t_ms / 56u) & 0xFFFFu);   // 約18px/s
    // パレット位相 t/32: 約 8.2 秒で 256 段を一巡する（速すぎるとチカチカする・遅すぎると
    // 静止画に見える、の間を取った初期値。実機の見た目で調整してよい）。
    *out_phase = (t_ms / 32u) & 0xFFu;
}

uint8_t fractal_at(int dx, int dy, uint32_t phase) {
    // 細かい層＋1/4 縮尺の粗い層。どちらも XOR なので位相 0・ドリフト 0 では x/y 対称
    // （テストで固定）。和は高々 16bit 級×3 で int を溢れない。
    const int fine   = dx ^ dy;
    const int coarse = (dx >> 2) ^ (dy >> 2);
    return static_cast<uint8_t>((static_cast<uint32_t>(fine + coarse) + phase) & 0xFFu);
}

uint8_t fractal_value(int x, int y, uint32_t t_ms) {
    int dx0 = 0, dy0 = 0;
    uint32_t phase = 0;
    fractal_offsets(t_ms, &dx0, &dy0, &phase);
    return fractal_at(x + dx0, y + dy0, phase);
}

uint32_t fractal_shade(uint32_t tone, uint8_t v) {
    const uint32_t r = ((tone >> 16) & 0xFFu) * v / 255u;
    const uint32_t g = ((tone >> 8) & 0xFFu) * v / 255u;
    const uint32_t b = (tone & 0xFFu) * v / 255u;
    return (r << 16) | (g << 8) | b;
}

uint8_t fractal_gamma(uint8_t v) {
    return static_cast<uint8_t>(static_cast<uint32_t>(v) * v / 255u);
}
