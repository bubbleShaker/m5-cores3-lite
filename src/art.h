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

// ───────── フローフィールド曲線（Issue #42 / #34 M3・Tyler Hobbs "Fidenza" 系） ─────────
// ノイズの流れ場に沿って無数の細い曲線を流す。ここでは「角度場」と「配色」だけを純粋関数で
// 決め、曲線の積分と実描画は main.cpp（M5.Display/M5Canvas）の責務に分離する＝ハード非依存で
// テストできる（M2 の図形生成と同じ責務分離）。

// なめらかな3D値ノイズ。戻り値はおよそ [-1, 1]。決定論的（同じ引数 → 必ず同じ値）。
//   z（時間軸）を少しずつ進めると場が連続的に変形する＝アニメーションの素になる。
//   ※ 値ノイズ＝格子点の擬似乱数値を 5次スムーズステップで補間する手法（Perlin の簡易版）。
float art_value_noise(float x, float y, float z);

// 流れ場の「その地点で曲線が進む向き（ラジアン）」を返す純粋関数。
//   ノイズを角度へ写像し、seed で場全体をずらす（作品ごとに別の流れ）。t は時間（z 軸へ流す）。
float art_flow_angle(float x, float y, float t, uint32_t seed);

// 配色：手選びの調和パレットを seed で選ぶ（ランダム多色にせず少数色＝「おしゃれ」の肝）。
constexpr int kFlowPaletteSize = 5;                 // 1パレットの線色数
uint16_t art_flow_background(uint32_t seed);        // 背景色（RGB565）
uint16_t art_flow_color(uint32_t seed, int index);  // index 本目の線色（パレットを巡回）
