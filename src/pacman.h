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

// ───────── ゲーム状態（Step3：ドット回収＋スコア） ─────────
// プレイヤー位置・進行方向・スコア・回収済みマスをまとめて持つ。実機描画には依存せず、
// 「動く／食べる／得点する」を純粋関数として native でテストできる（gem3d と同じ思想）。

// eaten 配列のコンパイル時サイズ。実迷路 15x13 に余裕を持たせた固定上限。
// 迷路をこれ以上大きくする時はここを広げること（pac_maze_w/h が超えると回収状態を保持できない）。
constexpr int kPacMaxW = 32;
constexpr int kPacMaxH = 32;

// 得点（本家準拠：ドット10点／パワーエサ50点）。
constexpr int kPacScoreDot   = 10;
constexpr int kPacScorePower = 50;

struct PacGame {
    Pos  player;                       // 現在のマス
    Dir  dir;                          // 実際に進んでいる方向
    int  score;                        // 累計スコア
    int  dots_left;                    // 残りドット＋パワーエサ数（0でクリア＝Step5で使用）
    bool eaten[kPacMaxH][kPacMaxW];    // 回収済みマス（true=食べた）。[y][x] の順で添字する
};

// ゲームを初期化して返す。プレイヤーは初期位置、スコア0、全ペレット未回収。
PacGame pac_game_init();

// 現在「見た目上」のタイル。元がドット/パワーエサでも回収済みなら Empty を返す。
//   壁・範囲外は pac_tile_at と同じく Wall（回収状態に依存しない）。
Tile pac_current_tile(const PacGame& g, int x, int y);

// desired 方向へ1ティック進める純粋関数（状態 g を更新する）。
//   曲がれるなら desired へ方向転換し、進めるなら1マス動く。
//   入った先が未回収のペレットなら食べて score/dots_left/eaten を更新する。
//   実際に動いたら true、壁などで動けなければ false を返す。
bool pac_game_advance(PacGame& g, Dir desired);
