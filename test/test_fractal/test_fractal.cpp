#include <unity.h>
#include "fractal.h"

// フラクタル背景模様（純粋ロジック・Issue #199）の単体テスト。
// XOR（マンチング・スクエア）のシェルピンスキー構造は実機で目視するしかないが、
// 「決定論・対称性・時間/空間で変化する・着色の両端と単調性」は性質として固定できる
// （art_value_noise のテストと同じ流儀: 特定値の一致ではなく性質で担保する）。

void setUp(void) {}
void tearDown(void) {}

// --- fractal_value ---

// 同じ引数なら必ず同じ値（決定論的）
void test_value_is_deterministic() {
    TEST_ASSERT_EQUAL_UINT8(fractal_value(37, 91, 12345u),
                            fractal_value(37, 91, 12345u));
}

// t=0 では x/y 対称（XOR の交換法則がそのまま出る＝構造が座標軸に対して素直である証拠）
void test_value_symmetric_at_t0() {
    for (int i = 0; i < 200; ++i) {
        const int x = (i * 7) % 320;
        const int y = (i * 13) % 240;
        TEST_ASSERT_EQUAL_UINT8(fractal_value(x, y, 0u), fractal_value(y, x, 0u));
    }
}

// 空間方向に変化する（定数模様ではない）
void test_value_varies_in_space() {
    const uint8_t base = fractal_value(0, 0, 0u);
    bool varied = false;
    for (int x = 1; x < 64 && !varied; ++x) {
        if (fractal_value(x, 0, 0u) != base) varied = true;
    }
    TEST_ASSERT_TRUE(varied);
}

// 時間方向に変化する（動的な模様である＝#199 の要求そのもの）
void test_value_varies_in_time() {
    const uint8_t base = fractal_value(100, 100, 0u);
    bool varied = false;
    for (uint32_t t = 100; t < 5000 && !varied; t += 100) {
        if (fractal_value(100, 100, t) != base) varied = true;
    }
    TEST_ASSERT_TRUE(varied);
}

// millis() が大きくなっても壊れない（uint32 の上限近くでも呼べて決定論的）
void test_value_survives_large_t() {
    TEST_ASSERT_EQUAL_UINT8(fractal_value(10, 20, 0xFFFFFF00u),
                            fractal_value(10, 20, 0xFFFFFF00u));
}

// 16bit マスクは出力に影響しない（fractal_offsets のコメントの非自明な主張を固定する・
// reviewer 指摘）。マスク無しの 64bit 参照実装と、マスクの折り返し境界（t/40 が
// 0xFFFF→0x10000 を跨ぐ t≈2,621,440ms）の前後を含む広い範囲で突き合わせる。
// 将来ドリフト除数を変えてこの前提が崩れたら、コメントではなくこのテストが割れる。
static uint8_t reference_value(int x, int y, uint32_t t) {
    const int64_t dx = static_cast<int64_t>(x) + t / 40u;   // マスク無し
    const int64_t dy = static_cast<int64_t>(y) + t / 56u;
    const int64_t fine   = dx ^ dy;
    const int64_t coarse = (dx >> 2) ^ (dy >> 2);
    return static_cast<uint8_t>((fine + coarse + t / 32u) & 0xFF);
}
void test_value_matches_unmasked_reference() {
    const uint32_t ts[] = { 0u, 1234u, 2621000u, 2621440u, 2622000u,
                            0x7FFFFFFFu, 0x80000000u, 0xFFFFFF00u };
    for (size_t i = 0; i < sizeof(ts) / sizeof(ts[0]); ++i) {
        for (int p = 0; p < 100; ++p) {
            const int x = (p * 13) % 320, y = (p * 29) % 240;
            TEST_ASSERT_EQUAL_UINT8(reference_value(x, y, ts[i]),
                                    fractal_value(x, y, ts[i]));
        }
    }
}

// 1フレーム内で階調が広く出る（位相だけ動いて模様が死んでいる改悪を検出する・reviewer 指摘）
void test_value_uses_wide_range_in_one_frame() {
    bool seen[256] = {};
    int uniq = 0;
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            const uint8_t v = fractal_value(x, y, 1234u);
            if (!seen[v]) { seen[v] = true; uniq++; }
        }
    }
    TEST_ASSERT_TRUE(uniq >= 200);  // 実測は 256。余裕を持って「広い」ことだけ固定する
}

// 分割版（offsets + at）は合成版 fractal_value と完全一致（表示側が使うのは分割版）
void test_split_matches_combined() {
    int dx0 = 0, dy0 = 0;
    uint32_t phase = 0;
    fractal_offsets(98765u, &dx0, &dy0, &phase);
    for (int p = 0; p < 200; ++p) {
        const int x = (p * 7) % 320, y = (p * 11) % 240;
        TEST_ASSERT_EQUAL_UINT8(fractal_value(x, y, 98765u),
                                fractal_at(x + dx0, y + dy0, phase));
    }
}

// --- fractal_gray ---
// （トーン着色 fractal_shade は #203 の白黒統一で1成分版 fractal_gray に置き換えた。
//   固定する性質は同じ: 両端・単調・上限超過なし）

// 両端: v=0 は 0、v=255 は max そのもの
void test_gray_endpoints() {
    TEST_ASSERT_EQUAL_UINT8(0, fractal_gray(120, 0));
    TEST_ASSERT_EQUAL_UINT8(120, fractal_gray(120, 255));
    TEST_ASSERT_EQUAL_UINT8(0, fractal_gray(0, 255));  // max=0 なら常に真っ黒
}

// v について単調非減少、かつ max を超えない（明るさの段階が逆転しない・帯が主役を食わない）
void test_gray_monotonic_and_capped() {
    const uint8_t maxes[] = { 40, 120, 255 };
    for (size_t m = 0; m < sizeof(maxes) / sizeof(maxes[0]); ++m) {
        uint8_t prev = 0;
        for (int v = 0; v <= 255; ++v) {
            const uint8_t g = fractal_gray(maxes[m], static_cast<uint8_t>(v));
            TEST_ASSERT_TRUE(g >= prev);
            TEST_ASSERT_TRUE(g <= maxes[m]);
            prev = g;
        }
    }
}

// --- fractal_gamma ---

// 両端固定・単調非減少（パレットの明暗の順序が崩れない）
void test_gamma_endpoints_and_monotonic() {
    TEST_ASSERT_EQUAL_UINT8(0, fractal_gamma(0));
    TEST_ASSERT_EQUAL_UINT8(255, fractal_gamma(255));
    uint8_t prev = 0;
    for (int v = 0; v <= 255; ++v) {
        const uint8_t g = fractal_gamma(static_cast<uint8_t>(v));
        TEST_ASSERT_TRUE(g >= prev);
        TEST_ASSERT_TRUE(g <= v);  // 暗部を締める＝入力より明るくはならない
        prev = g;
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_value_is_deterministic);
    RUN_TEST(test_value_symmetric_at_t0);
    RUN_TEST(test_value_varies_in_space);
    RUN_TEST(test_value_varies_in_time);
    RUN_TEST(test_value_survives_large_t);
    RUN_TEST(test_value_matches_unmasked_reference);
    RUN_TEST(test_value_uses_wide_range_in_one_frame);
    RUN_TEST(test_split_matches_combined);
    RUN_TEST(test_gray_endpoints);
    RUN_TEST(test_gray_monotonic_and_capped);
    RUN_TEST(test_gamma_endpoints_and_monotonic);
    return UNITY_END();
}
