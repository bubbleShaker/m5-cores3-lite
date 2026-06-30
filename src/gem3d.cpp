#include "gem3d.h"

#include <cmath>

// オイラー角での頂点回転。適用順は z→y→x。
// 各軸の回転は標準的な2D回転（その軸を固定して残り2成分を回す）を順番に掛けるだけ。
Vec3 gem3d_rotate(const Vec3& p, float ax, float ay, float az) {
    float x = p.x;
    float y = p.y;
    float z = p.z;

    // z 軸まわり: (x, y) を回す。
    float cz = std::cos(az);
    float sz = std::sin(az);
    float x1 = x * cz - y * sz;
    float y1 = x * sz + y * cz;
    x = x1;
    y = y1;

    // y 軸まわり: (z, x) を回す。
    float cy = std::cos(ay);
    float sy = std::sin(ay);
    float z2 = z * cy - x * sy;
    float x2 = z * sy + x * cy;
    z = z2;
    x = x2;

    // x 軸まわり: (y, z) を回す。
    float cx = std::cos(ax);
    float sx = std::sin(ax);
    float y3 = y * cx - z * sx;
    float z3 = y * sx + z * cx;
    y = y3;
    z = z3;

    return Vec3{x, y, z};
}

// 透視投影。scale = d / (d + z) を x,y に掛ける。
Vec2 gem3d_project(const Vec3& p, float d) {
    float denom = d + p.z;
    // カメラ位置と同じか手前（denom<=0）の点は投影が破綻するので原点に潰す。
    if (denom <= 0.0f) {
        return Vec2{0.0f, 0.0f};
    }
    float scale = d / denom;
    return Vec2{p.x * scale, p.y * scale};
}

// 裏面カリング。投影後三角形の符号付き面積（外積）の符号で表裏を見る。
bool gem3d_is_backface(const Vec2& a, const Vec2& b, const Vec2& c) {
    float cross = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    return cross <= 0.0f;  // 正=反時計回り=表。0以下は裏 or 潰れた面なので捨てる。
}

// 面の代表深さ＝3頂点zの平均。
float gem3d_face_depth(const Vec3& a, const Vec3& b, const Vec3& c) {
    return (a.z + b.z + c.z) / 3.0f;
}

// 面の外向き法線（正規化）。(b-a)×(c-a) を求めて単位長に割り戻す。
Vec3 gem3d_face_normal(const Vec3& a, const Vec3& b, const Vec3& c) {
    float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    // 外積 u×v。
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len <= 0.0f) {
        return Vec3{0.0f, 0.0f, 0.0f};  // 退化面（面積0）。明るさ0として安全に倒す。
    }
    return Vec3{nx / len, ny / len, nz / len};
}

// 面の明るさ係数 [0,1]。法線と光方向を正規化して内積を取り、負は0にクランプする。
float gem3d_face_brightness(const Vec3& normal, const Vec3& light) {
    float nlen = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    float llen = std::sqrt(light.x * light.x + light.y * light.y + light.z * light.z);
    if (nlen <= 0.0f || llen <= 0.0f) {
        return 0.0f;  // どちらかが零ベクトル＝向きが定義できない。明るさ0。
    }
    float dot = (normal.x * light.x + normal.y * light.y + normal.z * light.z) / (nlen * llen);
    if (dot < 0.0f) return 0.0f;  // 裏から当たる光は当たらない扱い。
    if (dot > 1.0f) return 1.0f;  // 数値誤差での>1を抑える。
    return dot;
}

// 正八面体の静的データ。
// 頂点は各軸の±単位ベクトル: 0:+x 1:-x 2:+y 3:-y 4:+z 5:-z。
static const Vec3 kOctaVerts[] = {
    { 1.0f,  0.0f,  0.0f},  // 0 +x
    {-1.0f,  0.0f,  0.0f},  // 1 -x
    { 0.0f,  1.0f,  0.0f},  // 2 +y
    { 0.0f, -1.0f,  0.0f},  // 3 -y
    { 0.0f,  0.0f,  1.0f},  // 4 +z
    { 0.0f,  0.0f, -1.0f},  // 5 -z
};

// 8面（各オクタントに1枚）。使う軸頂点は固定で、符号の偶奇に応じて
// 2頂点を入れ替え、全ての面の法線が外を向く（外から見て反時計回り）ようにしてある。
static const Tri kOctaTris[] = {
    {0, 2, 4},  // +++  (x,y,z)
    {0, 5, 2},  // ++-
    {0, 4, 3},  // +-+
    {0, 3, 5},  // +--
    {1, 4, 2},  // -++
    {1, 2, 5},  // -+-
    {1, 3, 4},  // --+
    {1, 5, 3},  // ---
};

Mesh gem3d_octahedron() {
    Mesh m;
    m.verts  = kOctaVerts;
    m.vcount = static_cast<int>(sizeof(kOctaVerts) / sizeof(kOctaVerts[0]));
    m.tris   = kOctaTris;
    m.tcount = static_cast<int>(sizeof(kOctaTris) / sizeof(kOctaTris[0]));
    return m;
}
