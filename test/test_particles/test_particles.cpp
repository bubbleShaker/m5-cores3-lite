#include <unity.h>
#include <cmath>
#include "particles.h"

void setUp(void) {}
void tearDown(void) {}

// count を capacity へクランプし、返り値＝書いた数
void test_count_clamped_to_capacity() {
    Particle p[4];
    TEST_ASSERT_EQUAL_INT(4, particle_ring(p, 4, 10, 100, 100, 90, 0.0f, 1.0f));
    TEST_ASSERT_EQUAL_INT(0, particle_ring(p, 0, 10, 100, 100, 90, 0.0f, 1.0f));
    TEST_ASSERT_EQUAL_INT(0, particle_ring(p, 4, 0, 100, 100, 90, 0.0f, 1.0f));
}

// intensity=0 は明るさ 0（実質見えない＝発話していない時に粒子が消える）
void test_zero_intensity_is_dark() {
    Particle p[8];
    const int n = particle_ring(p, 8, 8, 100, 100, 90, 1.23f, 0.0f);
    for (int i = 0; i < n; ++i) TEST_ASSERT_EQUAL_UINT8(0, p[i].level);
}

// intensity を上げると全体的に明るくなる（連動の可視化）
void test_higher_intensity_is_brighter() {
    Particle lo[8], hi[8];
    particle_ring(lo, 8, 8, 100, 100, 90, 0.0f, 0.3f);
    particle_ring(hi, 8, 8, 100, 100, 90, 0.0f, 1.0f);
    int sumLo = 0, sumHi = 0;
    for (int i = 0; i < 8; ++i) { sumLo += lo[i].level; sumHi += hi[i].level; }
    TEST_ASSERT_GREATER_THAN_INT(sumLo, sumHi);
}

// 粒子は中心からおおむね baseRadius 付近（脈動 ±11px 以内）に載る
void test_particles_on_ring_band() {
    Particle p[12];
    const int cx = 160, cy = 120, base = 90;
    const int n = particle_ring(p, 12, 12, cx, cy, base, 0.5f, 1.0f);
    for (int i = 0; i < n; ++i) {
        const float d = std::sqrt(float((p[i].x - cx) * (p[i].x - cx) +
                                        (p[i].y - cy) * (p[i].y - cy)));
        TEST_ASSERT_TRUE(d >= base - 12 && d <= base + 12);
    }
}

// 半径は最低 2px（描画で潰れない）、intensity で少し太る
void test_radius_floor_and_growth() {
    Particle lo[4], hi[4];
    particle_ring(lo, 4, 4, 100, 100, 90, 0.0f, 0.0f);
    particle_ring(hi, 4, 4, 100, 100, 90, 0.0f, 1.0f);
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(2, lo[i].radius);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(lo[i].radius, hi[i].radius);
    }
}

// 楕円：横半径>縦半径にすると、粒子の横の広がりが縦より大きくなる（Issue #116）
void test_ellipse_wider_than_tall() {
    Particle p[16];
    const int cx = 160, cy = 124;
    const int n = particle_ring(p, 16, 16, cx, cy, 116, 0.0f, 0.0f, 84);
    int maxDx = 0, maxDy = 0;
    for (int i = 0; i < n; ++i) {
        const int dx = std::abs(p[i].x - cx);
        const int dy = std::abs(p[i].y - cy);
        if (dx > maxDx) maxDx = dx;
        if (dy > maxDy) maxDy = dy;
    }
    TEST_ASSERT_GREATER_THAN_INT(maxDy, maxDx);        // 横のほうが広い
    TEST_ASSERT_GREATER_OR_EQUAL_INT(110, maxDx);      // 横半径ぶん外へ出る
}

// baseRadiusY 省略（負値）なら従来どおり真円（後方互換）
void test_omitted_ry_is_circle() {
    Particle e[8], c[8];
    particle_ring(e, 8, 8, 100, 100, 90, 0.7f, 0.5f, -1);  // ry=-1 → 円
    particle_ring(c, 8, 8, 100, 100, 90, 0.7f, 0.5f);       // 既定引数 → 円
    for (int i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_INT(c[i].x, e[i].x);
        TEST_ASSERT_EQUAL_INT(c[i].y, e[i].y);
    }
}

// 決定論的：同じ引数なら必ず同じ結果
void test_deterministic() {
    Particle a[8], b[8];
    particle_ring(a, 8, 8, 100, 100, 90, 2.5f, 0.7f);
    particle_ring(b, 8, 8, 100, 100, 90, 2.5f, 0.7f);
    for (int i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_INT(a[i].x, b[i].x);
        TEST_ASSERT_EQUAL_INT(a[i].y, b[i].y);
        TEST_ASSERT_EQUAL_UINT8(a[i].level, b[i].level);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_count_clamped_to_capacity);
    RUN_TEST(test_zero_intensity_is_dark);
    RUN_TEST(test_higher_intensity_is_brighter);
    RUN_TEST(test_particles_on_ring_band);
    RUN_TEST(test_radius_floor_and_growth);
    RUN_TEST(test_ellipse_wider_than_tall);
    RUN_TEST(test_omitted_ry_is_circle);
    RUN_TEST(test_deterministic);
    return UNITY_END();
}
