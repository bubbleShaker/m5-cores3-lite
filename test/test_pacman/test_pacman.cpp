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
    return UNITY_END();
}
