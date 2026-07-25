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

// --- fractal_shade ---

// 両端: v=0 は黒、v=255 はトーン色そのもの
void test_shade_endpoints() {
    TEST_ASSERT_EQUAL_UINT32(0u, fractal_shade(0x80C0FF, 0));
    TEST_ASSERT_EQUAL_UINT32(0x80C0FFu, fractal_shade(0x80C0FF, 255));
}

// 各成分は v について単調非減少（明るさの段階が逆転しない）
void test_shade_monotonic_per_channel() {
    const uint32_t tone = 0x60A0E0;
    uint32_t prev = 0;
    for (int v = 0; v <= 255; ++v) {
        const uint32_t c = fractal_shade(tone, static_cast<uint8_t>(v));
        TEST_ASSERT_TRUE(((c >> 16) & 0xFF) >= ((prev >> 16) & 0xFF));
        TEST_ASSERT_TRUE(((c >> 8) & 0xFF) >= ((prev >> 8) & 0xFF));
        TEST_ASSERT_TRUE((c & 0xFF) >= (prev & 0xFF));
        prev = c;
    }
}

// tone の 24bit を超えるビットは無視される（video_bg_tone と同じ契約）
void test_shade_masks_high_bits() {
    TEST_ASSERT_EQUAL_UINT32(fractal_shade(0x00123456u, 200),
                             fractal_shade(0xFF123456u, 200));
}

// 出力の各成分は入力トーンを超えない（v<255 で必ず暗くなる方向）
void test_shade_never_exceeds_tone() {
    const uint32_t tone = 0x40FF80;
    for (int v = 0; v <= 255; v += 5) {
        const uint32_t c = fractal_shade(tone, static_cast<uint8_t>(v));
        TEST_ASSERT_TRUE(((c >> 16) & 0xFF) <= 0x40u);
        TEST_ASSERT_TRUE(((c >> 8) & 0xFF) <= 0xFFu);
        TEST_ASSERT_TRUE((c & 0xFF) <= 0x80u);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_value_is_deterministic);
    RUN_TEST(test_value_symmetric_at_t0);
    RUN_TEST(test_value_varies_in_space);
    RUN_TEST(test_value_varies_in_time);
    RUN_TEST(test_value_survives_large_t);
    RUN_TEST(test_shade_endpoints);
    RUN_TEST(test_shade_monotonic_per_channel);
    RUN_TEST(test_shade_masks_high_bits);
    RUN_TEST(test_shade_never_exceeds_tone);
    return UNITY_END();
}
