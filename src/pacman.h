#pragma once

#include <cstdint>

// パックマン自作シーンの純粋ロジック層（Issue #134 / Step1）。
// 実機描画（M5GFX）や入力（タッチ）には一切依存しない。迷路の「意味」と
// プレイヤーの移動可否だけを持つので、native で単体テストできる。
// gem3d が 3D 数学を純粋層に切り出しているのと同じ思想で、バグりやすい
// ゲームロジックを PC 上で高速に検証できるようにするのが狙い。

// 迷路の1マスが何であるか。描画色ではなく「意味」を持つ（色は描画層の責務）。
enum class Tile : uint8_t {
    Wall,   // 壁（通れない）
    Dot,    // ドット（回収でスコア）
    Power,  // パワーエサ（回収でゴースト逃走・Step5で使用）
    Empty,  // 何もない床（通れる）
};

// 移動方向。None は「止まっている」を表す（初期状態や壁に阻まれた時）。
enum class Dir : uint8_t { None, Up, Down, Left, Right };

// 迷路のマス座標。左上が (0,0)、右へ +x、下へ +y。
struct Pos {
    int x;
    int y;
};

// 迷路の幅（マス数）。
int pac_maze_w();
// 迷路の高さ（マス数）。
int pac_maze_h();

// (x,y) のタイル種別を返す。
//   範囲外は Wall を返す（境界の外は壁とみなす安全側の既定値）。これにより
//   移動判定側が範囲チェックを重複して書かずに済む。
Tile pac_tile_at(int x, int y);

// プレイヤーの初期位置（迷路データ内の 'P' の場所）。
Pos pac_player_start();

// p から方向 d へ1マス動けるか。
//   Dir::None は動けない（false）。移動先が壁 or 範囲外なら false。
//   ドット/パワーエサ/床の上へは動ける（回収はここでは行わない＝純粋な可否判定）。
bool pac_can_move(Pos p, Dir d);

// p から d へ1マス進めた結果の位置を返す。
//   動けない時（pac_can_move が false）は p のまま返す（すり抜けない）。
Pos pac_step(Pos p, Dir d);
