#pragma once
#include <cstdint>
#include <vector>

// カラフル幾何学アート（テーマ N / Issue #34）の純粋生成ロジック。
// 「どんな図形を・どこに・どの大きさ・何色で」だけを決め、実際の描画は
// main.cpp（M5.Display）の責務に分離する＝ハード非依存でテストできる。

// 画面サイズ（CoreS3-Lite: 320x240）。全図形はこの矩形内に完全に収める。
constexpr int kArtScreenW = 320;
constexpr int kArtScreenH = 240;

// 図形サイズ（半径 / 半幅）の範囲。
constexpr int kArtMinSize = 8;
constexpr int kArtMaxSize = 48;

// 図形の種類。描画(main.cpp)はこの enum で fillTriangle/fillCircle/fillRect を呼び分ける。
enum class ShapeKind { Triangle, Circle, Rect };

// 1個の図形プリミティブ。座標は中心基準（円=中心、矩形/三角=外接の中心）。
struct Shape {
    ShapeKind kind;
    int16_t   cx;     // 中心 X
    int16_t   cy;     // 中心 Y
    int16_t   size;   // 半径 / 半幅
    uint16_t  color;  // RGB565
};

// シードから決定論的に図形列を生成する純粋関数。
//   同じ seed → 必ず同じ結果（再現可能・テスト可能）。
//   全図形は画面矩形内に完全に収まり、色は鮮やかなパレットから選ぶ。
// millis() 等に依存させず seed を引数で受けることで、実機なしで単体テストできる。
// count <= 0 のときは空のリストを返す。
std::vector<Shape> art_generate(uint32_t seed, int count);
