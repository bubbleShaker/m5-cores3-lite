#include <unity.h>
#include <cstring>
#include <vector>
#include "pacman.h"

void setUp(void) {}
void tearDown(void) {}

// 床（壁以外）とみなせるか。
static bool is_floor(int x, int y) {
    return pac_tile_at(x, y) != Tile::Wall;
}

// 迷路は正の大きさを持つ
void test_maze_has_positive_size() {
    TEST_ASSERT_GREATER_THAN_INT(0, pac_maze_w());
    TEST_ASSERT_GREATER_THAN_INT(0, pac_maze_h());
}

// 迷路の外周は全て壁（プレイヤーが場外へ出られない）
void test_border_is_all_wall() {
    const int w = pac_maze_w();
    const int h = pac_maze_h();
    for (int x = 0; x < w; ++x) {
        TEST_ASSERT_EQUAL(static_cast<int>(Tile::Wall), static_cast<int>(pac_tile_at(x, 0)));
        TEST_ASSERT_EQUAL(static_cast<int>(Tile::Wall), static_cast<int>(pac_tile_at(x, h - 1)));
    }
    for (int y = 0; y < h; ++y) {
        TEST_ASSERT_EQUAL(static_cast<int>(Tile::Wall), static_cast<int>(pac_tile_at(0, y)));
        TEST_ASSERT_EQUAL(static_cast<int>(Tile::Wall), static_cast<int>(pac_tile_at(w - 1, y)));
    }
}

// 範囲外はどこを聞いても壁（安全な既定値）
void test_out_of_range_is_wall() {
    TEST_ASSERT_EQUAL(static_cast<int>(Tile::Wall), static_cast<int>(pac_tile_at(-1, 0)));
    TEST_ASSERT_EQUAL(static_cast<int>(Tile::Wall), static_cast<int>(pac_tile_at(0, -1)));
    TEST_ASSERT_EQUAL(static_cast<int>(Tile::Wall), static_cast<int>(pac_tile_at(pac_maze_w(), 0)));
    TEST_ASSERT_EQUAL(static_cast<int>(Tile::Wall), static_cast<int>(pac_tile_at(0, pac_maze_h())));
}

// プレイヤー初期位置は壁ではない（詰み配置でない）
void test_player_start_is_not_wall() {
    const Pos s = pac_player_start();
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(Tile::Wall), static_cast<int>(pac_tile_at(s.x, s.y)));
}

// 迷路には回収対象（ドット or パワーエサ）が1つ以上ある
void test_maze_has_pellets() {
    int pellets = 0;
    for (int y = 0; y < pac_maze_h(); ++y) {
        for (int x = 0; x < pac_maze_w(); ++x) {
            Tile t = pac_tile_at(x, y);
            if (t == Tile::Dot || t == Tile::Power) ++pellets;
        }
    }
    TEST_ASSERT_GREATER_THAN_INT(0, pellets);
}

// Dir::None では動けず、その場に留まる
void test_none_direction_does_not_move() {
    const Pos s = pac_player_start();
    TEST_ASSERT_FALSE(pac_can_move(s, Dir::None));
    const Pos after = pac_step(s, Dir::None);
    TEST_ASSERT_EQUAL_INT(s.x, after.x);
    TEST_ASSERT_EQUAL_INT(s.y, after.y);
}

// 壁へは進めず、pac_step しても位置が変わらない（すり抜け防止）
void test_cannot_move_into_wall() {
    // (1,1) は床である前提（外周のすぐ内側の角）。前提自体もアサートして
    // サイレントパス（if 内が一度も実行されず緑になる）を防ぐ。
    const Pos p{1, 1};
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(Tile::Wall), static_cast<int>(pac_tile_at(p.x, p.y)));
    // 左＝(0,1)＝外周壁 への移動は不可
    TEST_ASSERT_FALSE(pac_can_move(p, Dir::Left));
    const Pos after = pac_step(p, Dir::Left);
    TEST_ASSERT_EQUAL_INT(p.x, after.x);
    TEST_ASSERT_EQUAL_INT(p.y, after.y);
}

// 全ての床がプレイヤー初期位置から到達できる（孤立マスが無い）。
// これは Step5 の「全ドット回収でクリア」を成立させるための不変条件で、
// レビューで見つかった (7,7) 孤立バグの再発防止テストでもある。
void test_all_floor_reachable_from_start() {
    const int w = pac_maze_w();
    const int h = pac_maze_h();

    // 床の総数を数える
    int floor_total = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (is_floor(x, y)) ++floor_total;

    // プレイヤー初期位置から BFS で到達できる床を数える
    std::vector<std::vector<bool>> seen(h, std::vector<bool>(w, false));
    std::vector<Pos> stack;
    const Pos s = pac_player_start();
    seen[s.y][s.x] = true;
    stack.push_back(s);
    int reached = 0;
    const int dx[] = {0, 0, -1, 1};
    const int dy[] = {-1, 1, 0, 0};
    while (!stack.empty()) {
        const Pos cur = stack.back();
        stack.pop_back();
        ++reached;
        for (int i = 0; i < 4; ++i) {
            const int nx = cur.x + dx[i];
            const int ny = cur.y + dy[i];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            if (seen[ny][nx] || !is_floor(nx, ny)) continue;
            seen[ny][nx] = true;
            stack.push_back(Pos{nx, ny});
        }
    }

    // 到達数が床の総数と一致 ＝ 孤立した床が無い
    TEST_ASSERT_EQUAL_INT(floor_total, reached);
}

// 全行の幅が pac_maze_w() と一致する（先頭行から導出した幅で全行を安全に読める）。
// pac_tile_at は kMazeW を信頼して添字アクセスするため、行がズレると NUL を跨ぐ危険がある。
// 各行の右端 (w-1) が壁である外周条件と併せ、行長の食い違いを検出する。
void test_all_rows_have_full_width() {
    const int w = pac_maze_w();
    const int h = pac_maze_h();
    for (int y = 0; y < h; ++y) {
        // 右端が読めて壁であること（行が短ければ NUL 由来で壁にならず落ちる）
        TEST_ASSERT_EQUAL(static_cast<int>(Tile::Wall), static_cast<int>(pac_tile_at(w - 1, y)));
    }
}

// 床方向へは1マスだけ正しく進む
void test_moves_one_tile_into_open() {
    const Pos s = pac_player_start();
    // 初期位置の周囲で、動ける方向を1つ見つけて「ちょうど1マス」進むことを確認する。
    const Dir dirs[] = {Dir::Up, Dir::Down, Dir::Left, Dir::Right};
    bool tested = false;
    for (Dir d : dirs) {
        if (!pac_can_move(s, d)) continue;
        const Pos after = pac_step(s, d);
        // マンハッタン距離がちょうど1（斜めにも2マスにも飛ばない）
        const int dist = (after.x - s.x) * (after.x - s.x) + (after.y - s.y) * (after.y - s.y);
        TEST_ASSERT_EQUAL_INT(1, dist);
        tested = true;
        break;
    }
    TEST_ASSERT_TRUE_MESSAGE(tested, "player start has no open neighbor");
}

// ───────── Step3：ゲーム状態（ドット回収＋スコア） ─────────

// 初期状態：スコア0、残ペレット>0、プレイヤーは初期位置、まだ何も食べていない
void test_game_init_state() {
    const PacGame g = pac_game_init();
    TEST_ASSERT_EQUAL_INT(0, g.score);
    TEST_ASSERT_GREATER_THAN_INT(0, g.dots_left);
    const Pos s = pac_player_start();
    TEST_ASSERT_EQUAL_INT(s.x, g.player.x);
    TEST_ASSERT_EQUAL_INT(s.y, g.player.y);
    // 初期位置のタイルは見た目上もペレットではない（'P' は床扱い）
    TEST_ASSERT_EQUAL(static_cast<int>(Tile::Empty),
                      static_cast<int>(pac_current_tile(g, s.x, s.y)));
}

// 初期の残ペレット数は迷路上のドット＋パワーエサ総数と一致する
void test_dots_left_matches_maze() {
    int pellets = 0;
    for (int y = 0; y < pac_maze_h(); ++y)
        for (int x = 0; x < pac_maze_w(); ++x) {
            const Tile t = pac_tile_at(x, y);
            if (t == Tile::Dot || t == Tile::Power) ++pellets;
        }
    const PacGame g = pac_game_init();
    TEST_ASSERT_EQUAL_INT(pellets, g.dots_left);
}

// 隣接するペレットへ進むと、加点され残数が減り、そのマスは見た目上 Empty になる
void test_advance_eats_pellet() {
    PacGame g = pac_game_init();

    // 初期位置から動ける方向を1つ選び、その先が元々ペレットのマスを探す
    const Dir dirs[] = {Dir::Left, Dir::Right, Dir::Up, Dir::Down};
    bool tested = false;
    for (Dir d : dirs) {
        if (!pac_can_move(g.player, d)) continue;
        const Pos next = pac_step(g.player, d);
        const Tile t = pac_tile_at(next.x, next.y);
        if (t != Tile::Dot && t != Tile::Power) continue;

        const int before_score = g.score;
        const int before_left  = g.dots_left;
        const int gain = (t == Tile::Power) ? kPacScorePower : kPacScoreDot;

        const bool moved = pac_game_advance(g, d);
        TEST_ASSERT_TRUE(moved);
        TEST_ASSERT_EQUAL_INT(next.x, g.player.x);
        TEST_ASSERT_EQUAL_INT(next.y, g.player.y);
        TEST_ASSERT_EQUAL_INT(before_score + gain, g.score);
        TEST_ASSERT_EQUAL_INT(before_left - 1, g.dots_left);
        // 食べたマスは見た目上 Empty になる（描画層が再びドットを描かないため）
        TEST_ASSERT_EQUAL(static_cast<int>(Tile::Empty),
                          static_cast<int>(pac_current_tile(g, next.x, next.y)));
        tested = true;
        break;
    }
    TEST_ASSERT_TRUE_MESSAGE(tested, "no adjacent pellet from start to test eating");
}

// 同じペレットマスを再訪しても二重加点しない
void test_no_double_scoring() {
    PacGame g = pac_game_init();

    // ドットへ1マス進んで食べる（Left へ進める迷路前提。念のため到達可能な方向を選ぶ）
    Dir eat_dir = Dir::None;
    const Dir dirs[] = {Dir::Left, Dir::Right, Dir::Up, Dir::Down};
    for (Dir d : dirs) {
        if (!pac_can_move(g.player, d)) continue;
        const Pos next = pac_step(g.player, d);
        if (pac_tile_at(next.x, next.y) == Tile::Dot) { eat_dir = d; break; }
    }
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(Dir::None), static_cast<int>(eat_dir));

    pac_game_advance(g, eat_dir);        // ドットを食べる
    const int score_after_eat = g.score;
    const int left_after_eat  = g.dots_left;

    // 逆向きに戻ってから、同じマスへ再訪する
    const Dir back = (eat_dir == Dir::Left) ? Dir::Right :
                     (eat_dir == Dir::Right) ? Dir::Left :
                     (eat_dir == Dir::Up) ? Dir::Down : Dir::Up;
    pac_game_advance(g, back);           // 元のマスへ戻る（'P'＝床、加点なし）
    pac_game_advance(g, eat_dir);        // 食べ済みマスへ再訪

    TEST_ASSERT_EQUAL_INT(score_after_eat, g.score);      // 加点されない
    TEST_ASSERT_EQUAL_INT(left_after_eat, g.dots_left);   // 残数も変わらない
}

// 壁向きの advance は動かず（false）、スコアも変わらない
void test_advance_into_wall_no_move() {
    PacGame g = pac_game_init();
    g.player = Pos{1, 1};  // 外周のすぐ内側。左＝外周壁
    const int before = g.score;
    const bool moved = pac_game_advance(g, Dir::Left);
    TEST_ASSERT_FALSE(moved);
    TEST_ASSERT_EQUAL_INT(1, g.player.x);
    TEST_ASSERT_EQUAL_INT(1, g.player.y);
    TEST_ASSERT_EQUAL_INT(before, g.score);
}

// ───────── Step4：ゴースト（追跡AIと衝突） ─────────

// 初期状態：局面は Playing、ゴーストは壁でない床に居る
void test_ghost_init_state() {
    const PacGame g = pac_game_init();
    TEST_ASSERT_EQUAL(static_cast<int>(PacPhase::Playing), static_cast<int>(g.phase));
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(Tile::Wall),
                          static_cast<int>(pac_tile_at(g.ghost.pos.x, g.ghost.pos.y)));
}

// 追跡AIが返す方向は必ず壁でない方向（ゴーストは壁にめり込まない）
void test_ghost_ai_never_picks_wall() {
    PacGame g = pac_game_init();
    // プレイヤーは動かさず、ゴーストだけ多数ティック進めても常に床の上に居る
    for (int i = 0; i < 60; ++i) {
        const Dir d = pac_ghost_next_dir(g);
        if (d == Dir::None) break;  // 完全に囲まれた（この迷路では起きない想定）
        TEST_ASSERT_TRUE(pac_can_move(g.ghost.pos, d));
        g.ghost.dir = d;
        g.ghost.pos = pac_step(g.ghost.pos, d);
        TEST_ASSERT_NOT_EQUAL(static_cast<int>(Tile::Wall),
                              static_cast<int>(pac_tile_at(g.ghost.pos.x, g.ghost.pos.y)));
    }
}

// 停止しているプレイヤーは、追跡ゴーストにいずれ捕まる（AIが実際に近づいている証拠）
void test_stationary_player_gets_caught() {
    PacGame g = pac_game_init();
    bool caught = false;
    // 迷路は 15x13＝最大距離でも数十歩。十分な回数ティックすれば必ず捕獲されるはず。
    for (int i = 0; i < 200; ++i) {
        pac_game_tick(g, Dir::None);  // プレイヤーは入力なし＝その場に留まる
        if (g.phase == PacPhase::Dead) { caught = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(caught, "ghost failed to catch a stationary player");
}

// プレイヤーが自らゴーストのマスへ突っ込むと、そのティックで捕獲され Dead になる
// （ゴーストは逆走禁止なので「真後ろ」からは寄って来ない。衝突判定そのものを検証するため
//   プレイヤー側から重ねるシナリオにする）。
void test_walking_into_ghost_catches() {
    PacGame g = pac_game_init();
    // プレイヤーが進める方向 d を選び、その隣接マスにゴーストを置く
    const Dir dirs[] = {Dir::Left, Dir::Right, Dir::Up, Dir::Down};
    Dir into = Dir::None;
    for (Dir d : dirs) {
        if (!pac_can_move(g.player, d)) continue;
        g.ghost.pos = pac_step(g.player, d);
        into = d;
        break;
    }
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(Dir::None), static_cast<int>(into));

    pac_game_tick(g, into);  // プレイヤーが d 方向へ1マス＝ゴーストのマスへ重なる
    TEST_ASSERT_EQUAL(static_cast<int>(PacPhase::Dead), static_cast<int>(g.phase));
}

// 決着後（Dead）は tick しても状態が動かない
void test_tick_is_noop_after_dead() {
    PacGame g = pac_game_init();
    g.phase = PacPhase::Dead;
    const Pos pp = g.player;
    const Pos gp = g.ghost.pos;
    const PacTickResult r = pac_game_tick(g, Dir::Right);
    TEST_ASSERT_FALSE(r.player_moved);
    TEST_ASSERT_EQUAL_INT(pp.x, g.player.x);
    TEST_ASSERT_EQUAL_INT(pp.y, g.player.y);
    TEST_ASSERT_EQUAL_INT(gp.x, g.ghost.pos.x);
    TEST_ASSERT_EQUAL_INT(gp.y, g.ghost.pos.y);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_maze_has_positive_size);
    RUN_TEST(test_border_is_all_wall);
    RUN_TEST(test_out_of_range_is_wall);
    RUN_TEST(test_player_start_is_not_wall);
    RUN_TEST(test_maze_has_pellets);
    RUN_TEST(test_none_direction_does_not_move);
    RUN_TEST(test_cannot_move_into_wall);
    RUN_TEST(test_all_floor_reachable_from_start);
    RUN_TEST(test_all_rows_have_full_width);
    RUN_TEST(test_moves_one_tile_into_open);
    RUN_TEST(test_game_init_state);
    RUN_TEST(test_dots_left_matches_maze);
    RUN_TEST(test_advance_eats_pellet);
    RUN_TEST(test_no_double_scoring);
    RUN_TEST(test_advance_into_wall_no_move);
    RUN_TEST(test_ghost_init_state);
    RUN_TEST(test_ghost_ai_never_picks_wall);
    RUN_TEST(test_stationary_player_gets_caught);
    RUN_TEST(test_walking_into_ghost_catches);
    RUN_TEST(test_tick_is_noop_after_dead);
    return UNITY_END();
}
