#include "art.h"
#include <cstddef>
#include <cmath>

namespace {

// 線形合同法(LCG)による擬似乱数。<random> を使わず軽量・決定論的・移植容易。
// Numerical Recipes の係数。state を破壊的に更新しつつ次の値を返す。
uint32_t lcg_next(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state;
}

// [lo, hi]（両端含む）の整数を返す。hi >= lo を前提とする。
int rand_range(uint32_t& state, int lo, int hi) {
    const uint32_t span = static_cast<uint32_t>(hi - lo + 1);
    return lo + static_cast<int>(lcg_next(state) % span);
}

// 鮮やかな配色パレット(RGB565)。幾何学アートの「カラフル」さを担保する。
constexpr uint16_t kPalette[] = {
    0xF800, // 赤
    0xFD20, // 橙
    0xFFE0, // 黄
    0x07E0, // 緑
    0x07FF, // 水
    0x001F, // 青
    0x781F, // 紫
    0xF81F, // 桃
};
constexpr int kPaletteCount = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

} // namespace

std::vector<Shape> art_generate(uint32_t seed, int count) {
    std::vector<Shape> shapes;
    if (count <= 0) return shapes;
    shapes.reserve(static_cast<size_t>(count));

    uint32_t state = seed;
    for (int i = 0; i < count; ++i) {
        Shape s;
        s.kind = static_cast<ShapeKind>(rand_range(state, 0, 2));
        s.size = static_cast<int16_t>(rand_range(state, kArtMinSize, kArtMaxSize));
        // 画面内に完全に収まるよう、中心を size 分だけ内側に制限する。
        s.cx    = static_cast<int16_t>(rand_range(state, s.size, kArtScreenW - s.size));
        s.cy    = static_cast<int16_t>(rand_range(state, s.size, kArtScreenH - s.size));
        s.color = kPalette[rand_range(state, 0, kPaletteCount - 1)];
        shapes.push_back(s);
    }
    return shapes;
}

// ───────── フローフィールド曲線（Issue #42 / #34 M3） ─────────

namespace {

// 整数格子点 (ix,iy,iz) → [-1,1] の擬似乱数値。固定係数で決定論的・移植容易。
float hash3(int ix, int iy, int iz) {
    uint32_t n = static_cast<uint32_t>(ix) * 374761393u
               + static_cast<uint32_t>(iy) * 668265263u
               + static_cast<uint32_t>(iz) * 2147483647u
               + 1013904223u;
    n = (n ^ (n >> 13)) * 1274126177u;
    n ^= (n >> 16);
    // 下位24bitを [0,1) に正規化し、[-1,1] へ展開する。
    return (static_cast<float>(n & 0x00FFFFFFu) / 16777215.0f) * 2.0f - 1.0f;
}

// 5次スムーズステップ（Perlin の fade）。0..1 を端で滑らかにつなぐ補間係数。
float fade5(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// RGB888 を RGB565 にする（パレットを読みやすく書くため）。
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// 手選びの調和パレット（背景 + 5線色）。少数色で上品にまとめるのが Fidenza 系の肝。
struct FlowPalette {
    uint16_t bg;
    uint16_t colors[kFlowPaletteSize];
};
constexpr FlowPalette kFlowPalettes[] = {
    // 1) クリーム地 × 暖色テラコッタ（Fidenza 定番の温かみ）
    { rgb565(0xF2, 0xE9, 0xD8), { rgb565(0xC0, 0x65, 0x4A), rgb565(0xE8, 0xA0, 0x6A),
        rgb565(0xEA, 0xC9, 0xA0), rgb565(0x5A, 0x46, 0x36), rgb565(0x8C, 0x7A, 0x4A) } },
    // 2) 紺地 × 寒色ティール/サーモン
    { rgb565(0x10, 0x24, 0x32), { rgb565(0x3A, 0xA8, 0x9E), rgb565(0x7E, 0xC8, 0xC0),
        rgb565(0xF2, 0x8C, 0x6B), rgb565(0xF4, 0xD0, 0x6F), rgb565(0xAE, 0xC6, 0xCF) } },
    // 3) 黒地 × 少数ネオン（青緑/桃/黄/緑）
    { rgb565(0x0A, 0x0A, 0x0E), { rgb565(0x2E, 0xE6, 0xD6), rgb565(0xFF, 0x4F, 0xA3),
        rgb565(0xFF, 0xE0, 0x55), rgb565(0x6B, 0xE0, 0x7A), rgb565(0x9B, 0x8C, 0xFF) } },
    // 4) 生成り × くすみパステル（リソグラフ風）
    { rgb565(0xF4, 0xEF, 0xE6), { rgb565(0xB5, 0x6B, 0x73), rgb565(0xD4, 0x8E, 0x5C),
        rgb565(0x6F, 0x9B, 0xA8), rgb565(0xE0, 0xBA, 0x6E), rgb565(0x7B, 0x8C, 0x6E) } },
};
constexpr int kFlowPaletteCount = static_cast<int>(sizeof(kFlowPalettes) / sizeof(kFlowPalettes[0]));

} // namespace

float art_value_noise(float x, float y, float z) {
    const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
    const float tx = fade5(x - fx), ty = fade5(y - fy), tz = fade5(z - fz);

    // 立方体の8頂点の値をトライリニア補間する（各軸を fade5 で滑らかに）。
    const float c000 = hash3(ix,     iy,     iz);
    const float c100 = hash3(ix + 1, iy,     iz);
    const float c010 = hash3(ix,     iy + 1, iz);
    const float c110 = hash3(ix + 1, iy + 1, iz);
    const float c001 = hash3(ix,     iy,     iz + 1);
    const float c101 = hash3(ix + 1, iy,     iz + 1);
    const float c011 = hash3(ix,     iy + 1, iz + 1);
    const float c111 = hash3(ix + 1, iy + 1, iz + 1);

    const float x00 = lerpf(c000, c100, tx);
    const float x10 = lerpf(c010, c110, tx);
    const float x01 = lerpf(c001, c101, tx);
    const float x11 = lerpf(c011, c111, tx);
    const float y0  = lerpf(x00, x10, ty);
    const float y1  = lerpf(x01, x11, ty);
    return lerpf(y0, y1, tz);
}

float art_flow_angle(float x, float y, float t, uint32_t seed) {
    // seed で場全体を平行移動（作品ごとに別の流れ）。scale が小さいほど大きくゆるやかな流れ。
    const float ox    = static_cast<float>(seed & 0xFFFFu) * 0.013f;
    const float oy    = static_cast<float>((seed >> 16) & 0xFFFFu) * 0.013f;
    const float scale = 0.006f;
    const float n = art_value_noise(x * scale + ox, y * scale + oy, t);
    // ノイズ[-1,1] を約1.5回転に写像（曲線のうねりの強さ）。
    return n * 3.14159265f * 1.5f;
}

uint16_t art_flow_background(uint32_t seed) {
    return kFlowPalettes[seed % kFlowPaletteCount].bg;
}

uint16_t art_flow_color(uint32_t seed, int index) {
    if (index < 0) index = -index;
    return kFlowPalettes[seed % kFlowPaletteCount].colors[index % kFlowPaletteSize];
}
