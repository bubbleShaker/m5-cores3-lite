#include <unity.h>
#include "video_fade.h"

// 曲選択カルーセルのフェード遷移（Issue #209）の状態機械を検証する。
// スワイプ → フェードアウト → 暗転の底で差し替え → フェードイン、の遷移と
// 輝度（0..256）だけの純粋ロジックなので、時刻を進めながら native で固める。

void setUp(void) {}
void tearDown(void) {}

namespace {
constexpr uint32_t kDur = 160;  // 実機の想定値と同じ長さで検証する
}

// 初期状態は等倍（256）・切り替え要求なし
void test_idle_is_full_level() {
    VideoFade f;
    video_fade_init(&f);
    int swap_to = 99;
    TEST_ASSERT_EQUAL_UINT32(256, video_fade_level(&f, 1000, kDur, &swap_to));
    TEST_ASSERT_EQUAL_INT(-1, swap_to);
}

// スワイプでフェードアウトが始まり、輝度が単調に下がる
void test_request_starts_fade_out() {
    VideoFade f;
    video_fade_init(&f);
    video_fade_request(&f, 1000, 3, kDur);
    int swap_to = -1;
    TEST_ASSERT_EQUAL_UINT32(256, video_fade_level(&f, 1000, kDur, &swap_to));  // 起点は等倍
    TEST_ASSERT_EQUAL_UINT32(128, video_fade_level(&f, 1000 + kDur / 2, kDur, &swap_to));
    TEST_ASSERT_EQUAL_INT(-1, swap_to);  // まだ底ではない
    // 単調に下がる（途中で明るくならない）
    uint32_t prev = 256;
    for (uint32_t t = 0; t < kDur; t += 10) {
        VideoFade g;
        video_fade_init(&g);
        video_fade_request(&g, 1000, 3, kDur);
        int s = -1;
        const uint32_t level = video_fade_level(&g, 1000 + t, kDur, &s);
        TEST_ASSERT_TRUE(level <= prev);
        TEST_ASSERT_TRUE(level <= 256);
        prev = level;
    }
}

// 暗転の底で行き先が1回だけ返り、フェードインへ移って等倍まで戻る
void test_swap_at_bottom_then_fade_in() {
    VideoFade f;
    video_fade_init(&f);
    video_fade_request(&f, 1000, 7, kDur);
    int swap_to = -1;
    TEST_ASSERT_EQUAL_UINT32(0, video_fade_level(&f, 1000 + kDur, kDur, &swap_to));
    TEST_ASSERT_EQUAL_INT(7, swap_to);  // 底＝呼び出し側がサムネイルを差し替える瞬間
    // 以降は swap_to は出ない
    swap_to = -1;
    TEST_ASSERT_EQUAL_UINT32(128, video_fade_level(&f, 1000 + kDur + kDur / 2, kDur, &swap_to));
    TEST_ASSERT_EQUAL_INT(-1, swap_to);
    // フェードイン完了で等倍・アイドルへ（それ以降も等倍のまま）
    TEST_ASSERT_EQUAL_UINT32(256, video_fade_level(&f, 1000 + kDur * 2, kDur, &swap_to));
    TEST_ASSERT_EQUAL_UINT32(256, video_fade_level(&f, 1000 + kDur * 9, kDur, &swap_to));
    TEST_ASSERT_EQUAL_INT(-1, swap_to);
}

// フェードアウト中の連打は暗転をやり直さず行き先だけ差し替える
void test_retarget_during_fade_out() {
    VideoFade f;
    video_fade_init(&f);
    video_fade_request(&f, 1000, 3, kDur);
    int swap_to = -1;
    video_fade_level(&f, 1000 + kDur / 2, kDur, &swap_to);        // 半分まで暗転
    video_fade_request(&f, 1000 + kDur / 2, 5, kDur);             // 連打で行き先変更
    // 底は最初の起点から kDur 後のまま（やり直していない）
    TEST_ASSERT_EQUAL_UINT32(0, video_fade_level(&f, 1000 + kDur, kDur, &swap_to));
    TEST_ASSERT_EQUAL_INT(5, swap_to);                            // 最新の行き先が返る
}

// フェードイン中のスワイプは「今の明るさ」から連続にフェードアウトへ折り返す
void test_request_during_fade_in_is_continuous() {
    VideoFade f;
    video_fade_init(&f);
    video_fade_request(&f, 1000, 3, kDur);
    int swap_to = -1;
    video_fade_level(&f, 1000 + kDur, kDur, &swap_to);            // 底 → kIn へ
    const uint32_t t_mid = 1000 + kDur + kDur / 4;                // イン途中（輝度 64）
    const uint32_t before = video_fade_level(&f, t_mid, kDur, &swap_to);
    TEST_ASSERT_EQUAL_UINT32(64, before);
    video_fade_request(&f, t_mid, 9, kDur);                       // 折り返し
    const uint32_t after = video_fade_level(&f, t_mid, kDur, &swap_to);
    TEST_ASSERT_EQUAL_UINT32(before, after);                      // 明るさが跳ばない
    // そのまま下がりきって新しい行き先へ
    TEST_ASSERT_EQUAL_UINT32(0, video_fade_level(&f, t_mid + kDur, kDur, &swap_to));
    TEST_ASSERT_EQUAL_INT(9, swap_to);
}

// 「今 UI が向かっている曲」: フェードアウト中は行き先、それ以外は現在値
void test_target_resolution() {
    VideoFade f;
    video_fade_init(&f);
    TEST_ASSERT_EQUAL_INT(2, video_fade_target(&f, 2));           // idle → 現在値
    video_fade_request(&f, 1000, 6, kDur);
    TEST_ASSERT_EQUAL_INT(6, video_fade_target(&f, 2));           // kOut → 行き先
    int swap_to = -1;
    video_fade_level(&f, 1000 + kDur, kDur, &swap_to);            // 底 → kIn
    TEST_ASSERT_EQUAL_INT(2, video_fade_target(&f, 2));           // kIn → 現在値（差し替え済み）
}

// dur=0 は「フェード無し」＝要求済みの行き先を即座に返して等倍のまま
void test_zero_duration_swaps_immediately() {
    VideoFade f;
    video_fade_init(&f);
    video_fade_request(&f, 1000, 4, 0);
    int swap_to = -1;
    TEST_ASSERT_EQUAL_UINT32(256, video_fade_level(&f, 1000, 0, &swap_to));
    TEST_ASSERT_EQUAL_INT(4, swap_to);
}

// null 安全（防御。呼び出し側のバグでも落ちない）
void test_null_safety() {
    int swap_to = -1;
    TEST_ASSERT_EQUAL_UINT32(256, video_fade_level(nullptr, 1000, kDur, &swap_to));
    TEST_ASSERT_EQUAL_INT(-1, swap_to);
    TEST_ASSERT_EQUAL_INT(8, video_fade_target(nullptr, 8));
    video_fade_init(nullptr);                    // 落ちなければよい
    video_fade_request(nullptr, 1000, 3, kDur);  // 同上
    VideoFade f;
    video_fade_init(&f);
    video_fade_request(&f, 1000, 3, kDur);
    TEST_ASSERT_EQUAL_UINT32(128, video_fade_level(&f, 1000 + kDur / 2, kDur, nullptr));
    TEST_ASSERT_FALSE(video_fade_needs_redraw(nullptr));
    video_fade_rebase(nullptr, 1000);  // 落ちなければよい
}

// 🔴 だった性質: フェード完了のコマでも「等倍で描き直す」を1回だけ要求する。
// これが無いと、最後に適用した暗い絵が次のスワイプまで残り続ける（表示用 Sprite は
// サムネイル読み直し以外で書き換わらないため）。
void test_needs_redraw_once_after_settle() {
    VideoFade f;
    video_fade_init(&f);
    TEST_ASSERT_FALSE(video_fade_needs_redraw(&f));  // 初期化直後は不要（呼び出し側が絵を作る）

    video_fade_request(&f, 1000, 3, kDur);
    int swap_to = -1;
    TEST_ASSERT_TRUE(video_fade_needs_redraw(&f));   // kOut 中は毎コマ
    video_fade_level(&f, 1000 + kDur, kDur, &swap_to);
    TEST_ASSERT_TRUE(video_fade_needs_redraw(&f));   // kIn 中も毎コマ

    // 完了のコマ: level は 256 に戻り、描き直しは「1回だけ」要求される
    TEST_ASSERT_EQUAL_UINT32(256, video_fade_level(&f, 1000 + kDur * 2, kDur, &swap_to));
    TEST_ASSERT_TRUE(video_fade_needs_redraw(&f));
    TEST_ASSERT_FALSE(video_fade_needs_redraw(&f));  // 2回目以降は不要（毎コマ複写しない）

    // 次のスワイプでまた動き出す
    video_fade_request(&f, 2000, 5, kDur);
    TEST_ASSERT_TRUE(video_fade_needs_redraw(&f));
}

// kIn の起点打ち直し: 底で起点を打つと直後の SD 読み（数十ms）ぶん先に進んでしまうので、
// 読み終わりの時刻へ揃える。kIn 以外では何もしない。
void test_rebase_restarts_fade_in() {
    VideoFade f;
    video_fade_init(&f);
    video_fade_request(&f, 1000, 3, kDur);
    int swap_to = -1;
    video_fade_level(&f, 1000 + kDur, kDur, &swap_to);  // 底 → kIn（起点 1000+kDur）
    // サムネイル読みに 80ms かかったとして、打ち直さなければ既に半分まで進んでいる
    const uint32_t after_read = 1000 + kDur + 80;
    video_fade_rebase(&f, after_read);
    TEST_ASSERT_EQUAL_UINT32(0, video_fade_level(&f, after_read, kDur, &swap_to));  // 0 から始まる
    TEST_ASSERT_EQUAL_UINT32(128, video_fade_level(&f, after_read + kDur / 2, kDur, &swap_to));

    // kOut / kIdle では何もしない（起点を壊さない）
    VideoFade g;
    video_fade_init(&g);
    video_fade_request(&g, 1000, 3, kDur);
    video_fade_rebase(&g, 1000 + kDur / 2);
    TEST_ASSERT_EQUAL_UINT32(128, video_fade_level(&g, 1000 + kDur / 2, kDur, &swap_to));
}

// millis の 49.7 日ラップを実際に跨いでも輝度が飛ばない。
// 起点をラップの 64ms 前に置くので、kDur/2(80ms) 後・kDur(160ms) 後はどちらも
// 0 付近の小さな値へ回り込む（引き算が uint32 で正しく巻き戻ることの確認）。
void test_wraps_around_millis() {
    VideoFade f;
    video_fade_init(&f);
    const uint32_t start = 0xFFFFFFC0u;  // ラップの 64ms 前
    TEST_ASSERT_TRUE(start + kDur / 2 < start);  // 本当に跨いでいることを先に固定する
    video_fade_request(&f, start, 3, kDur);
    int swap_to = -1;
    TEST_ASSERT_EQUAL_UINT32(128, video_fade_level(&f, start + kDur / 2, kDur, &swap_to));
    TEST_ASSERT_EQUAL_UINT32(0, video_fade_level(&f, start + kDur, kDur, &swap_to));
    TEST_ASSERT_EQUAL_INT(3, swap_to);
}

// 起点より前の now（呼び出し順の都合で起こりうる）は経過 0 扱い。
// ガードが無いと uint32 のアンダーフローで巨大な経過になり、1コマだけデタラメな輝度が出る。
void test_now_before_start_is_full_level() {
    VideoFade f;
    video_fade_init(&f);
    video_fade_request(&f, 1000, 3, kDur);
    int swap_to = -1;
    TEST_ASSERT_EQUAL_UINT32(256, video_fade_level(&f, 990, kDur, &swap_to));  // 経過 0 ＝ 等倍
    TEST_ASSERT_EQUAL_INT(-1, swap_to);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_is_full_level);
    RUN_TEST(test_request_starts_fade_out);
    RUN_TEST(test_swap_at_bottom_then_fade_in);
    RUN_TEST(test_retarget_during_fade_out);
    RUN_TEST(test_request_during_fade_in_is_continuous);
    RUN_TEST(test_target_resolution);
    RUN_TEST(test_zero_duration_swaps_immediately);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_needs_redraw_once_after_settle);
    RUN_TEST(test_rebase_restarts_fade_in);
    RUN_TEST(test_wraps_around_millis);
    RUN_TEST(test_now_before_start_is_full_level);
    return UNITY_END();
}
