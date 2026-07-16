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

// ───────── ゲーム状態（Step3） ─────────

// (x,y) が eaten 配列の有効な添字か。範囲外アクセスを防ぐ共通ガード。
static bool in_eaten_bounds(int x, int y) {
    return x >= 0 && x < kPacMaxW && y >= 0 && y < kPacMaxH;
}

// ゴーストの初期マス。中央上の床（プレイヤー初期位置 (7,9) から十分離れている）。
// 迷路データ上は床（ドット）で、連結性テストにより必ずプレイヤーへ到達できる。
static const Pos kGhostStart{7, 3};

PacGame pac_game_init() {
    PacGame g{};  // 集約の値初期化で eaten を全 false、その他を 0 にする
    g.player    = pac_player_start();
    g.dir       = Dir::None;
    g.score     = 0;
    g.dots_left = 0;
    g.ghost.pos = kGhostStart;
    g.ghost.dir = Dir::Left;   // 初期の進行方向（逆走禁止の基準）
    g.phase     = PacPhase::Playing;
    // 迷路上のドット／パワーエサ総数を数え、クリア判定用の残数に入れる。
    for (int y = 0; y < pac_maze_h(); ++y) {
        for (int x = 0; x < pac_maze_w(); ++x) {
            const Tile t = pac_tile_at(x, y);
            if (t == Tile::Dot || t == Tile::Power) ++g.dots_left;
        }
    }
    return g;
}

Tile pac_current_tile(const PacGame& g, int x, int y) {
    const Tile t = pac_tile_at(x, y);
    // 回収済みのドット／パワーエサは床（Empty）として見せる。壁・範囲外はそのまま。
    if ((t == Tile::Dot || t == Tile::Power) && in_eaten_bounds(x, y) && g.eaten[y][x]) {
        return Tile::Empty;
    }
    return t;
}

bool pac_game_advance(PacGame& g, Dir desired) {
    // 曲がれる時だけ予約方向を採用。desired が Dir::None（予約なし）だと pac_can_move は
    // 常に false なので g.dir は維持され、慣性でそのまま直進し続ける（本家と同じ挙動）。
    if (pac_can_move(g.player, desired)) g.dir = desired;
    if (!pac_can_move(g.player, g.dir)) return false;      // 壁向きなら動かない
    g.player = pac_step(g.player, g.dir);

    // 入った先が未回収のペレットなら食べる（同じマスを再訪しても二重加点しない）。
    // ドットとパワーエサは得点だけ異なり、回収処理（eaten 記録・残数減）は共通。
    const int x = g.player.x, y = g.player.y;
    if (in_eaten_bounds(x, y) && !g.eaten[y][x]) {
        const Tile t = pac_tile_at(x, y);
        if (t == Tile::Dot || t == Tile::Power) {
            g.eaten[y][x] = true;
            g.score += (t == Tile::Power) ? kPacScorePower : kPacScoreDot;
            --g.dots_left;
        }
    }
    return true;
}

// ───────── ゴースト（Step4） ─────────

// 2マスが同じ位置か。
static bool same_tile(Pos a, Pos b) { return a.x == b.x && a.y == b.y; }

// 方向の逆。None の逆は None。
static Dir opposite(Dir d) {
    switch (d) {
        case Dir::Up:    return Dir::Down;
        case Dir::Down:  return Dir::Up;
        case Dir::Left:  return Dir::Right;
        case Dir::Right: return Dir::Left;
        default:         return Dir::None;
    }
}

// a と b のマンハッタン距離（|dx|+|dy|）。
static int manhattan(Pos a, Pos b) {
    const int dx = a.x - b.x, dy = a.y - b.y;
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
}

// 「まだどの方向も選んでいない」ことを表す番兵距離。迷路サイズ程度の実距離を必ず上回る。
static const int kPacDistInf = 1 << 30;

Dir pac_ghost_next_dir(const PacGame& g) {
    // タイブレークの優先順（本家準拠）。order を優先順に並べ、下で厳密小なり(<)で比較するので
    // 同距離のときは配列の先頭側＝優先度の高い方向が先勝ちになる（決定的な選択）。
    const Dir order[4] = {Dir::Up, Dir::Left, Dir::Down, Dir::Right};
    const Dir rev = opposite(g.ghost.dir);

    Dir best = Dir::None;
    int bestDist = kPacDistInf;  // 未選択を表す番兵。実距離は迷路サイズ程度でこれを超えない。
    // 逆走を除いた候補から、プレイヤーへ最も近づく方向を選ぶ。
    for (Dir d : order) {
        if (d == rev) continue;                       // 逆走は禁止（往復ジッタ防止）
        if (!pac_can_move(g.ghost.pos, d)) continue;  // 壁は避ける
        const int dist = manhattan(pac_step(g.ghost.pos, d), g.player);
        if (dist < bestDist) { bestDist = dist; best = d; }
    }
    if (best != Dir::None) return best;

    // 逆走以外に道が無い袋小路のときだけ、引き返す。
    if (pac_can_move(g.ghost.pos, rev)) return rev;
    return Dir::None;  // 完全に囲まれている（通常は起きない）
}

PacTickResult pac_game_tick(PacGame& g, Dir desired) {
    PacTickResult r{ g.player, g.ghost.pos, false };
    if (g.phase != PacPhase::Playing) return r;  // 決着後は何もしない

    // ① プレイヤーを先に進める。移動直後にゴーストのマスへ乗ったら捕獲（ゴーストは動かさず決着）。
    r.player_moved = pac_game_advance(g, desired);
    if (same_tile(g.player, g.ghost.pos)) { g.phase = PacPhase::Dead; return r; }

    // ② ゴーストを追跡AIに従って1マス進める。
    const Dir gd = pac_ghost_next_dir(g);
    if (gd != Dir::None) {
        g.ghost.dir = gd;
        g.ghost.pos = pac_step(g.ghost.pos, gd);
    }

    // ③ ゴースト移動後に同じマスなら捕獲。
    //    プレイヤー→ゴーストの逐次解決なので、これで「すれ違い（入れ替わり）」も取りこぼさない：
    //    もし両者が隣接マスを入れ替わるなら、①でプレイヤーがゴーストの旧マス（＝この時点のゴースト
    //    位置）へ乗るため、①の判定が必ず先に発火する。よって②→③は「ゴーストがプレイヤーへ突っ込む」
    //    ケースだけを担い、別途のすれ違い判定は不要（重複するだけ）。
    if (same_tile(g.player, g.ghost.pos)) g.phase = PacPhase::Dead;
    return r;
}
