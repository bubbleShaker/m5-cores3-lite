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
    RUN_TEST(test_deterministic);
    return UNITY_END();
}
