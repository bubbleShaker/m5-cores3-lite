#pragma once

// 宝石を3D回転表示するためのソフトウェア3D数学（epic #27・P3 / Issue #70・M1）。
//
// ESP32-S3 には GPU が無いので、頂点をCPUで回して投影し、面を塗って描く。
// このヘッダは「回す・投影する・面の表裏や奥行きを判定する」純粋計算だけを持つ。
// 実機描画（fillTriangle）には依存しないので native で単体テストできる（gem.cpp と同じ流儀）。

// 3D空間の点・ベクトル。
struct Vec3 {
    float x;
    float y;
    float z;
};

// 投影後の2D点（画面座標系ではなく、焦点距離で割っただけの正規化座標）。
struct Vec2 {
    float x;
    float y;
};

// 三角形1枚。多面体の面を三角形に分割して持つ（インデックスは Mesh.verts を指す）。
struct Tri {
    int a;
    int b;
    int c;
};

// 多面体メッシュ。頂点配列と三角形配列への参照だけを持つ軽量ビュー（所有しない）。
struct Mesh {
    const Vec3* verts;
    int         vcount;
    const Tri*  tris;
    int         tcount;
};

// 点 p をオイラー角 (ax, ay, az) ラジアンで回す。
// 適用順は z→y→x（まず z 軸、次に y 軸、最後に x 軸まわりに回す）。
// 行列ライブラリは持たず三角関数で直接合成する（依存ゼロで native でも動かすため）。
Vec3 gem3d_rotate(const Vec3& p, float ax, float ay, float az);

// 点 p を透視投影して2D座標に落とす。
//   d はカメラと原点の距離（焦点距離）。scale = d / (d + p.z) を x,y に掛ける。
//   奥（z大）ほど縮んで中心に寄り、z=0 の面は等倍で写る。
//   d + p.z が 0 以下（カメラ位置より手前）になる点は描けないので、(0,0) を返して安全側に倒す。
Vec2 gem3d_project(const Vec3& p, float d);

// 投影後の三角形 (a,b,c) が裏向き（カメラに背を向けている）かを判定する。
//   符号付き面積 cross = (b-a)×(c-a) が正なら反時計回り＝表向き → false。
//   cross <= 0 は裏向き、または真横で潰れた（面積0の）面なので true（＝描かずに捨てる）。
//   面を表向きで反時計回りに定義しておけば、これで見えない面の描画を省ける（コスト半減）。
bool gem3d_is_backface(const Vec2& a, const Vec2& b, const Vec2& c);

// 三角形 (a,b,c) の代表的な奥行き（3頂点のzの平均）を返す。
//   描画時にこの値で面を奥→手前にソートして塗れば（ペインターズアルゴリズム）、
//   zバッファ無しで重なり順を正しくできる。z が大きいほど奥なので先に塗る対象。
float gem3d_face_depth(const Vec3& a, const Vec3& b, const Vec3& c);

// 汎用ローポリ立体：正八面体（6頂点・8三角形）。
//   宝石らしいファセット感が出る最小の立体。返す Mesh は静的データを指すだけ（所有しない）。
//   全ての面は外から見て反時計回り（表向き）になるよう頂点順を定義してある
//   （gem3d_is_backface のカリング規約と整合）。
Mesh gem3d_octahedron();
