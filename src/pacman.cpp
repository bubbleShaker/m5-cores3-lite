#include "pacman.h"

#include <cstring>  // strlen（幅を先頭行長から導出する）

// 迷路データ（Issue #134 / Step1）。
// 記号: '#'=壁, '.'=ドット, 'o'=パワーエサ, ' '=床, 'P'=プレイヤー初期位置。
// 左右対称で、全ての床が連結している（孤立した袋小路が無い）ように手描きした。
// 幅15 × 高さ13。実機の 320x240 に素直に収まるサイズ（描画層は Step2 で載せる）。
// ※中央列(col7)は上下に貫通させ、パワーエサ(7,5)/(7,7)を本流と連結させている
//   （縦通路が無いと(7,7)が孤立して回収不能になるため。連結性は native テストで検証）。
static const char* const kMaze[] = {
    "###############",
    "#......#......#",
    "#.###.#.#.###.#",
    "#.............#",
    "#.###.###.###.#",
    "#...#..o..#...#",
    "###.#.#.#.#.###",
    "#.....#o#.....#",
    "#.###.###.###.#",
    "#......P......#",
    "#.###.#.#.###.#",
    "#......#......#",
    "###############",
};

// 迷路の高さ = 行数。幅 = 先頭行の長さから導出（マジックナンバーを避け、データと自動整合）。
// 全行が同じ長さである前提は native テスト（幅の一致検証）で守る。
static const int kMazeH = static_cast<int>(sizeof(kMaze) / sizeof(kMaze[0]));
static const int kMazeW = static_cast<int>(std::strlen(kMaze[0]));

int pac_maze_w() { return kMazeW; }
int pac_maze_h() { return kMazeH; }

Tile pac_tile_at(int x, int y) {
    // 範囲外は壁扱い（境界の外＝通れない）。移動判定側の範囲チェックを不要にする。
    if (x < 0 || x >= kMazeW || y < 0 || y >= kMazeH) return Tile::Wall;
    switch (kMaze[y][x]) {
        case '#': return Tile::Wall;
        case '.': return Tile::Dot;
        case 'o': return Tile::Power;
        // 'P'（プレイヤー初期位置）は床として扱う。プレイヤーはデータではなく
        // 状態（Pos）で持つので、マップ上の 'P' マスは通れる床でよい。
        case 'P': return Tile::Empty;
        default:  return Tile::Empty;  // ' ' やその他は床
    }
}

Pos pac_player_start() {
    for (int y = 0; y < kMazeH; ++y) {
        for (int x = 0; x < kMazeW; ++x) {
            if (kMaze[y][x] == 'P') return Pos{x, y};
        }
    }
    // 'P' が無いデータでも落ちないよう、安全な既定値（中央付近）を返す。
    return Pos{kMazeW / 2, kMazeH / 2};
}

// 方向 d に対応する1マス分の移動量。None は (0,0)（動かない）。
static Pos dir_delta(Dir d) {
    switch (d) {
        case Dir::Up:    return Pos{0, -1};
        case Dir::Down:  return Pos{0, +1};
        case Dir::Left:  return Pos{-1, 0};
        case Dir::Right: return Pos{+1, 0};
        case Dir::None:
        default:         return Pos{0, 0};
    }
}

bool pac_can_move(Pos p, Dir d) {
    if (d == Dir::None) return false;  // 止まっている時は動けない
    const Pos delta = dir_delta(d);
    const int nx = p.x + delta.x;
    const int ny = p.y + delta.y;
    // 移動先が壁（範囲外含む）なら不可。それ以外の床/ドット/パワーエサへは可。
    return pac_tile_at(nx, ny) != Tile::Wall;
}

Pos pac_step(Pos p, Dir d) {
    if (!pac_can_move(p, d)) return p;  // 動けない時はその場に留まる（すり抜け防止）
    const Pos delta = dir_delta(d);
    return Pos{p.x + delta.x, p.y + delta.y};
}
