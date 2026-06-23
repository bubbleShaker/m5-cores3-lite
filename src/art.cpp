#include "art.h"
#include <cstddef>

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
