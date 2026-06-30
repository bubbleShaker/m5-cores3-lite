#include <unity.h>
#include <cmath>
#include "gem3d.h"

void setUp(void) {}
void tearDown(void) {}

static const float PI = 3.14159265358979323846f;

// 回転角0なら点は動かない。
void test_rotate_zero_is_identity() {
    Vec3 p{1.0f, 2.0f, 3.0f};
    Vec3 r = gem3d_rotate(p, 0.0f, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, r.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.0f, r.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.0f, r.z);
}

// z軸まわり90度: (1,0,0) → (0,1,0)。
void test_rotate_z_90() {
    Vec3 r = gem3d_rotate(Vec3{1.0f, 0.0f, 0.0f}, 0.0f, 0.0f, PI / 2.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, r.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.z);
}

// y軸まわり90度: (1,0,0) → (0,0,-1)。
// 実装の y軸回転は z2 = z*cy - x*sy, x2 = z*sy + x*cy。
// (x,z)=(1,0), ay=90° → z2 = -1, x2 = 0。
void test_rotate_y_90() {
    Vec3 r = gem3d_rotate(Vec3{1.0f, 0.0f, 0.0f}, 0.0f, PI / 2.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -1.0f, r.z);
}

// x軸まわり90度: (0,1,0) → (0,0,1)。
// x軸回転は y3 = y*cx - z*sx, z3 = y*sx + z*cx。
// (y,z)=(1,0), ax=90° → y3 = 0, z3 = 1。
void test_rotate_x_90() {
    Vec3 r = gem3d_rotate(Vec3{0.0f, 1.0f, 0.0f}, PI / 2.0f, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, r.z);
}

// 回転はノルム（原点からの距離）を保存する。任意角でも長さは不変。
void test_rotate_preserves_norm() {
    Vec3 p{0.5f, -1.3f, 2.1f};
    float n0 = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    Vec3 r = gem3d_rotate(p, 0.7f, -1.1f, 2.4f);
    float n1 = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, n0, n1);
}

// z=0 の面は等倍で写る（scale=1）。
void test_project_at_zero_z_is_identity() {
    Vec2 r = gem3d_project(Vec3{1.5f, -2.0f, 0.0f}, 5.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.5f, r.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, -2.0f, r.y);
}

// 原点は投影しても原点（中心は動かない）。
void test_project_origin_stays_center() {
    Vec2 r = gem3d_project(Vec3{0.0f, 0.0f, 3.0f}, 5.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.y);
}

// 奥（z>0）にある点は中心へ縮む。手前（z<0）にある点は中心から離れて拡大する。
void test_project_depth_scales() {
    // 変数名は near/far を避ける（MSVC windef.h のレガシーマクロと衝突しうるため）。
    Vec3 p{2.0f, 0.0f, 0.0f};
    float zfar = gem3d_project(Vec3{p.x, p.y, 3.0f}, 5.0f).x;   // d/(d+z)=5/8 → 1.25
    float zmid = gem3d_project(Vec3{p.x, p.y, 0.0f}, 5.0f).x;   // 等倍 → 2.0
    float znear = gem3d_project(Vec3{p.x, p.y, -2.0f}, 5.0f).x; // 5/3 → 3.33...
    TEST_ASSERT_TRUE(zfar < zmid);
    TEST_ASSERT_TRUE(zmid < znear);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.25f, zfar);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f, zmid);
}

// カメラ位置と同じ深さ（d+z<=0）は破綻するので原点に潰して安全側へ。
void test_project_behind_camera_is_safe() {
    Vec2 r = gem3d_project(Vec3{1.0f, 1.0f, -5.0f}, 5.0f);  // d+z=0
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, r.y);
}

// 反時計回り（表向き）の三角形は裏面ではない → false。
void test_backface_ccw_is_front() {
    Vec2 a{0.0f, 0.0f};
    Vec2 b{1.0f, 0.0f};
    Vec2 c{0.0f, 1.0f};
    TEST_ASSERT_FALSE(gem3d_is_backface(a, b, c));
}

// 巻き順を逆（時計回り）にすると裏面 → true。
void test_backface_cw_is_back() {
    Vec2 a{0.0f, 0.0f};
    Vec2 b{0.0f, 1.0f};
    Vec2 c{1.0f, 0.0f};
    TEST_ASSERT_TRUE(gem3d_is_backface(a, b, c));
}

// 3点が一直線（面積0・真横）に潰れた面は捨てる → true。
void test_backface_degenerate_is_culled() {
    Vec2 a{0.0f, 0.0f};
    Vec2 b{1.0f, 1.0f};
    Vec2 c{2.0f, 2.0f};
    TEST_ASSERT_TRUE(gem3d_is_backface(a, b, c));
}

// 面の深さは3頂点zの平均。
void test_face_depth_is_average_z() {
    Vec3 a{0.0f, 0.0f, 1.0f};
    Vec3 b{0.0f, 0.0f, 2.0f};
    Vec3 c{0.0f, 0.0f, 6.0f};
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.0f, gem3d_face_depth(a, b, c));
}

// 奥（z大）の面ほど深さ値が大きい（ソートの大小関係が成り立つ）。
void test_face_depth_ordering() {
    Vec3 fa{0.0f, 0.0f, 5.0f}, fb{1.0f, 0.0f, 5.0f}, fc{0.0f, 1.0f, 5.0f};  // 奥
    Vec3 na{0.0f, 0.0f, 1.0f}, nb{1.0f, 0.0f, 1.0f}, nc{0.0f, 1.0f, 1.0f};  // 手前
    TEST_ASSERT_TRUE(gem3d_face_depth(fa, fb, fc) > gem3d_face_depth(na, nb, nc));
}

// 正八面体は頂点6・三角形8。
void test_octahedron_counts() {
    Mesh m = gem3d_octahedron();
    TEST_ASSERT_EQUAL_INT(6, m.vcount);
    TEST_ASSERT_EQUAL_INT(8, m.tcount);
}

// 全ての三角形インデックスが頂点配列の範囲内 [0, vcount)。
void test_octahedron_indices_in_range() {
    Mesh m = gem3d_octahedron();
    for (int i = 0; i < m.tcount; ++i) {
        const Tri& t = m.tris[i];
        TEST_ASSERT_TRUE(t.a >= 0 && t.a < m.vcount);
        TEST_ASSERT_TRUE(t.b >= 0 && t.b < m.vcount);
        TEST_ASSERT_TRUE(t.c >= 0 && t.c < m.vcount);
    }
}

// 全ての面が外向き（法線・重心の内積>0）＝外から見て反時計回りで定義されている。
// 原点中心の凸立体なので、外向き法線は重心方向と同じ側を向くはず。
void test_octahedron_faces_outward() {
    Mesh m = gem3d_octahedron();
    for (int i = 0; i < m.tcount; ++i) {
        const Vec3& a = m.verts[m.tris[i].a];
        const Vec3& b = m.verts[m.tris[i].b];
        const Vec3& c = m.verts[m.tris[i].c];
        // 法線 = (b-a) × (c-a)
        float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        // 重心
        float gx = (a.x + b.x + c.x) / 3.0f;
        float gy = (a.y + b.y + c.y) / 3.0f;
        float gz = (a.z + b.z + c.z) / 3.0f;
        float dot = nx * gx + ny * gy + nz * gz;
        TEST_ASSERT_TRUE(dot > 0.0f);
    }
}

// 面法線は (b-a)×(c-a) を正規化したもの。xy平面の表向き三角形なら +z 単位ベクトル。
void test_face_normal_basic() {
    Vec3 n = gem3d_face_normal(Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, n.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, n.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, n.z);
}

// 法線は常に単位長（任意の三角形でノルム1）。
void test_face_normal_is_unit() {
    Vec3 n = gem3d_face_normal(Vec3{1.0f, 0.0f, 2.0f}, Vec3{0.0f, 3.0f, 1.0f}, Vec3{-2.0f, 1.0f, 0.0f});
    float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, len);
}

// 退化面（3点が一直線）は法線が定義できず零ベクトル。
void test_face_normal_degenerate_is_zero() {
    Vec3 n = gem3d_face_normal(Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f}, Vec3{2.0f, 2.0f, 2.0f});
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, n.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, n.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, n.z);
}

// 正八面体の各面の外向き法線は重心方向と同じ側（内積>0）を向く。
void test_face_normal_octahedron_outward() {
    Mesh m = gem3d_octahedron();
    for (int i = 0; i < m.tcount; ++i) {
        const Vec3& a = m.verts[m.tris[i].a];
        const Vec3& b = m.verts[m.tris[i].b];
        const Vec3& c = m.verts[m.tris[i].c];
        Vec3 n = gem3d_face_normal(a, b, c);
        float gx = (a.x + b.x + c.x) / 3.0f;
        float gy = (a.y + b.y + c.y) / 3.0f;
        float gz = (a.z + b.z + c.z) / 3.0f;
        TEST_ASSERT_TRUE(n.x * gx + n.y * gy + n.z * gz > 0.0f);
    }
}

// 正面から光が当たる（法線と光が同方向）と明るさ1。
void test_brightness_full_when_aligned() {
    float b = gem3d_face_brightness(Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 0.0f, 1.0f});
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, b);
}

// 真横（直交）は明るさ0。
void test_brightness_zero_when_perpendicular() {
    float b = gem3d_face_brightness(Vec3{0.0f, 0.0f, 1.0f}, Vec3{1.0f, 0.0f, 0.0f});
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, b);
}

// 裏から当たる光（内積が負）は0にクランプ。
void test_brightness_clamped_when_backlit() {
    float b = gem3d_face_brightness(Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 0.0f, -1.0f});
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, b);
}

// 入力は内部で正規化される（長さの違う光ベクトルでも cos は同じ）。
void test_brightness_normalizes_inputs() {
    float b = gem3d_face_brightness(Vec3{0.0f, 0.0f, 2.0f}, Vec3{0.0f, 0.0f, 5.0f});
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, b);
}

// 零ベクトル（退化面の法線など）は明るさ0で安全に倒す。
void test_brightness_zero_vector_is_zero() {
    float b = gem3d_face_brightness(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f});
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, b);
}

// 45度から当たる光は cos45° ≈ 0.707。
void test_brightness_partial_angle() {
    float b = gem3d_face_brightness(Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 1.0f, 1.0f});
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.70710678f, b);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rotate_zero_is_identity);
    RUN_TEST(test_rotate_z_90);
    RUN_TEST(test_rotate_y_90);
    RUN_TEST(test_rotate_x_90);
    RUN_TEST(test_rotate_preserves_norm);
    RUN_TEST(test_project_at_zero_z_is_identity);
    RUN_TEST(test_project_origin_stays_center);
    RUN_TEST(test_project_depth_scales);
    RUN_TEST(test_project_behind_camera_is_safe);
    RUN_TEST(test_backface_ccw_is_front);
    RUN_TEST(test_backface_cw_is_back);
    RUN_TEST(test_backface_degenerate_is_culled);
    RUN_TEST(test_face_depth_is_average_z);
    RUN_TEST(test_face_depth_ordering);
    RUN_TEST(test_octahedron_counts);
    RUN_TEST(test_octahedron_indices_in_range);
    RUN_TEST(test_octahedron_faces_outward);
    RUN_TEST(test_face_normal_basic);
    RUN_TEST(test_face_normal_is_unit);
    RUN_TEST(test_face_normal_degenerate_is_zero);
    RUN_TEST(test_face_normal_octahedron_outward);
    RUN_TEST(test_brightness_full_when_aligned);
    RUN_TEST(test_brightness_zero_when_perpendicular);
    RUN_TEST(test_brightness_clamped_when_backlit);
    RUN_TEST(test_brightness_normalizes_inputs);
    RUN_TEST(test_brightness_zero_vector_is_zero);
    RUN_TEST(test_brightness_partial_angle);
    return UNITY_END();
}
