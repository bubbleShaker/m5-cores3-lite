#include <unity.h>
#include <cmath>
#include "art.h"

// フローフィールド曲線（Issue #42 / #34 M3）の純粋ロジックを検証する。
// 値ノイズは「決定論・範囲有界・連続（滑らか）」という性質で担保する
// （実機=xtensa と native=x86 で浮動小数の最終ビットは一致しないため、
//  特定値の一致ではなく性質テストにする）。

void setUp(void) {}
void tearDown(void) {}

// --- 値ノイズ ---

// 同じ引数なら必ず同じ値（決定論的）
void test_noise_is_deterministic() {
    TEST_ASSERT_EQUAL_FLOAT(art_value_noise(3.2f, 7.8f, 1.1f),
                            art_value_noise(3.2f, 7.8f, 1.1f));
}

// 戻り値はおよそ [-1, 1] に収まる（多数サンプル）
void test_noise_within_range() {
    for (int i = 0; i < 500; ++i) {
        const float x = static_cast<float>(i) * 0.37f;
        const float y = static_cast<float>(i) * 0.13f - 5.0f;
        const float z = static_cast<float>(i) * 0.05f;
        const float n = art_value_noise(x, y, z);
        TEST_ASSERT_TRUE(n >= -1.0001f && n <= 1.0001f);
    }
}

// 連続（滑らか）：入力を微小に動かすと出力も微小しか変わらない
void test_noise_is_continuous() {
    const float x = 4.317f, y = 2.911f, z = 0.523f;
    const float a = art_value_noise(x, y, z);
    const float b = art_value_noise(x + 0.01f, y, z);
    TEST_ASSERT_TRUE(std::fabs(a - b) < 0.1f);
}

// 定数ではない：離れた点では値が変化する
void test_noise_varies_over_space() {
    const float a = art_value_noise(0.5f, 0.5f, 0.0f);
    const float b = art_value_noise(12.5f, 30.5f, 0.0f);
    TEST_ASSERT_TRUE(std::fabs(a - b) > 1e-4f);
}

// 時間 z を進めると場が変化する（アニメの素）
void test_noise_changes_over_time() {
    const float a = art_value_noise(2.5f, 2.5f, 0.0f);
    const float b = art_value_noise(2.5f, 2.5f, 3.0f);
    TEST_ASSERT_TRUE(std::fabs(a - b) > 1e-4f);
}

// --- 流れ場の角度 ---

// 同じ引数なら同じ角度（決定論的）
void test_flow_angle_is_deterministic() {
    TEST_ASSERT_EQUAL_FLOAT(art_flow_angle(100.0f, 50.0f, 0.2f, 7u),
                            art_flow_angle(100.0f, 50.0f, 0.2f, 7u));
}

// seed が違えば（同じ地点でも）流れの向きが変わる
void test_flow_angle_differs_by_seed() {
    const float a = art_flow_angle(100.0f, 50.0f, 0.0f, 1u);
    const float b = art_flow_angle(100.0f, 50.0f, 0.0f, 9999u);
    TEST_ASSERT_TRUE(std::fabs(a - b) > 1e-4f);
}

// --- 配色 ---

// 背景・線色は決定論的（同じ seed で同じ色）
void test_palette_is_deterministic() {
    TEST_ASSERT_EQUAL_UINT16(art_flow_background(3u), art_flow_background(3u));
    TEST_ASSERT_EQUAL_UINT16(art_flow_color(3u, 2), art_flow_color(3u, 2));
}

// 線色インデックスはパレットサイズで巡回する
void test_palette_color_wraps() {
    TEST_ASSERT_EQUAL_UINT16(art_flow_color(5u, 0), art_flow_color(5u, kFlowPaletteSize));
    TEST_ASSERT_EQUAL_UINT16(art_flow_color(5u, 1), art_flow_color(5u, 1 + kFlowPaletteSize));
}

// 負のインデックスでも安全（範囲外アクセスしない＝絶対値で巡回）
void test_palette_color_negative_index_safe() {
    const uint16_t c = art_flow_color(2u, -1);
    TEST_ASSERT_EQUAL_UINT16(art_flow_color(2u, 1), c);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_noise_is_deterministic);
    RUN_TEST(test_noise_within_range);
    RUN_TEST(test_noise_is_continuous);
    RUN_TEST(test_noise_varies_over_space);
    RUN_TEST(test_noise_changes_over_time);
    RUN_TEST(test_flow_angle_is_deterministic);
    RUN_TEST(test_flow_angle_differs_by_seed);
    RUN_TEST(test_palette_is_deterministic);
    RUN_TEST(test_palette_color_wraps);
    RUN_TEST(test_palette_color_negative_index_safe);
    return UNITY_END();
}
