#include <unity.h>

#include <cmath>

#include "squeeze.h"

void setUp(void) {}
void tearDown(void) {}

// テスト用の既定フィールド（実機の画面と同じ 320x240）。
static SqueezeField field() {
    SqueezeField f;
    f.w      = 320.0f;
    f.h      = 240.0f;
    f.radius = 34.0f;
    return f;
}

// 一定の入力で n フレーム進める（1フレーム 33ms ≒ 30fps）。
static void advance(SqueezeState& s, const SqueezeField& f, float gx, float gy, float gz, int n) {
    SqueezeInput in;
    in.gx = gx;
    in.gy = gy;
    in.gz = gz;
    in.dt = 0.033f;
    for (int i = 0; i < n; ++i) squeeze_update(s, f, in);
}

// 中心がフィールド内（半径ぶん内側）に収まっているか。全テストで守るべき不変条件。
static bool inside(const SqueezeState& s, const SqueezeField& f) {
    return s.x >= f.radius - 0.01f && s.x <= f.w - f.radius + 0.01f &&
           s.y >= f.radius - 0.01f && s.y <= f.h - f.radius + 0.01f;
}

// リセット直後は中央・静止・無変形・非吸着
void test_reset_is_centered_and_idle() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, s.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, s.y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, s.vx);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, s.vy);
    TEST_ASSERT_EQUAL(static_cast<int>(SqueezeWall::None), static_cast<int>(s.wall));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, squeeze_scale_x(s));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, squeeze_scale_y(s));
}

// 平置き（画面を上に向けた静止状態）なら画面内では動かない。
// 入力を全ゼロにしないのは、|g|=0 が実機では「自由落下」を意味してシェイク判定に掛かるため。
// 静止状態は必ず |g|=1G になるので、面外の軸(gz)に 1G を置くのが実機と整合する。
void test_at_rest_flat_keeps_position() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    advance(s, f, 0.0f, 0.0f, 1.0f, 30);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, s.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, s.y);
}

// 右へ傾ける（gx>0）と右へ動く
void test_tilt_right_moves_right() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    advance(s, f, 1.0f, 0.0f, 0.0f, 5);
    TEST_ASSERT_TRUE(s.vx > 0.0f);
    TEST_ASSERT_TRUE(s.x > 160.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, s.y);  // 縦は動かない
}

// 上へ傾ける（gy<0）と上へ動く。4方向すべてが対称に効くことの確認。
void test_tilt_up_moves_up() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    advance(s, f, 0.0f, -1.0f, 0.0f, 5);
    TEST_ASSERT_TRUE(s.vy < 0.0f);
    TEST_ASSERT_TRUE(s.y < 120.0f);
}

// どれだけ強く長く引っ張っても画面外へは出ない
void test_never_escapes_field() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    advance(s, f, 3.0f, 3.0f, 0.0f, 300);
    TEST_ASSERT_TRUE(inside(s, f));
    advance(s, f, -3.0f, -3.0f, 0.0f, 300);
    TEST_ASSERT_TRUE(inside(s, f));
}

// 壁に当たると速度の符号が反転し、勢いは落ちる（反発係数 < 1）
void test_wall_bounces_and_damps() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    // 右へ加速して壁へ向かわせ、当たる直前の速度を控える
    float before = 0.0f;
    SqueezeInput in;
    in.gx = 1.0f;  // |g|=1G ＝ 真横に傾けきった状態。シェイクとは判定されない
    in.dt = 0.033f;
    for (int i = 0; i < 100; ++i) {
        const float prev_v = s.vx;
        squeeze_update(s, f, in);
        if (s.vx < 0.0f) {  // 跳ね返った
            before = prev_v;
            break;
        }
    }
    TEST_ASSERT_TRUE(before > 0.0f);            // 跳ね返りが起きた
    TEST_ASSERT_TRUE(s.vx < 0.0f);              // 向きが反転した
    TEST_ASSERT_TRUE(std::fabs(s.vx) < before); // 減衰した
}

// 壁に当たると潰れる（無変形ではなくなる）
void test_wall_hit_deforms() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 1.0f;
    in.dt = 0.033f;
    for (int i = 0; i < 100; ++i) {
        squeeze_update(s, f, in);
        if (s.vx < 0.0f) break;
    }
    TEST_ASSERT_TRUE(squeeze_scale_x(s) < 0.99f);  // 横方向に潰れている
    TEST_ASSERT_TRUE(squeeze_scale_y(s) > 1.01f);  // 縦に膨らんでいる
}

// 変形は時間が経てば丸（倍率1.0）へ戻る
void test_deform_returns_to_round() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 1.0f;
    in.dt = 0.033f;
    for (int i = 0; i < 100; ++i) {
        squeeze_update(s, f, in);
        if (s.vx < 0.0f) break;
    }
    TEST_ASSERT_TRUE(squeeze_scale_x(s) < 0.99f);
    advance(s, f, 0.0f, 0.0f, 1.0f, 120);  // 約4秒放置
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, squeeze_scale_x(s));
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, squeeze_scale_y(s));
}

// 傾けただけ（合力の大きさが 1G のまま）ではシェイクと誤認しない
void test_tilt_alone_never_sticks() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    // 斜め45度に傾けた状態＝合力の大きさは 1G
    const float k = 0.7071f;
    advance(s, f, k, k, 0.0f, 200);
    TEST_ASSERT_EQUAL(static_cast<int>(SqueezeWall::None), static_cast<int>(s.wall));
}

// 弱い揺れではくっつかない（キック閾値は超えるが吸着閾値には届かない）
void test_weak_shake_does_not_stick() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 2.0f;  // |g|=2G → 1G からのズレ 1.0。キック閾値は超えるが吸着閾値には届かない
    in.dt = 0.033f;
    squeeze_update(s, f, in);
    TEST_ASSERT_EQUAL(static_cast<int>(SqueezeWall::None), static_cast<int>(s.wall));
}

// 強く右へ振ると右の壁に吸着する
void test_strong_shake_sticks_to_right_wall() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 3.0f;  // |g| = 3G → 1G からのズレ 2.0 ≥ kSqShakeStick
    in.dt = 0.033f;
    squeeze_update(s, f, in);
    TEST_ASSERT_EQUAL(static_cast<int>(SqueezeWall::Right), static_cast<int>(s.wall));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, f.w - f.radius, s.x);
    TEST_ASSERT_TRUE(inside(s, f));
}

// 4方向すべてに吸着できる（支配的な軸で壁が決まる）
void test_strong_shake_sticks_to_each_wall() {
    const SqueezeField f = field();
    struct Case {
        float       gx, gy;
        SqueezeWall want;
    };
    const Case cases[] = {
        { -3.0f, 0.0f, SqueezeWall::Left },
        { 3.0f, 0.0f, SqueezeWall::Right },
        { 0.0f, -3.0f, SqueezeWall::Top },
        { 0.0f, 3.0f, SqueezeWall::Bottom },
    };
    for (const Case& c : cases) {
        SqueezeState s;
        squeeze_reset(s, f);
        SqueezeInput in;
        in.gx = c.gx;
        in.gy = c.gy;
        in.dt = 0.033f;
        squeeze_update(s, f, in);
        TEST_ASSERT_EQUAL(static_cast<int>(c.want), static_cast<int>(s.wall));
        TEST_ASSERT_TRUE(inside(s, f));
    }
}

// 画面に垂直（z軸）にだけ強く振っても、上下左右のどこにも勝手に貼り付かない
void test_z_only_shake_does_not_stick() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gz = 3.0f;
    in.dt = 0.033f;
    squeeze_update(s, f, in);
    TEST_ASSERT_EQUAL(static_cast<int>(SqueezeWall::None), static_cast<int>(s.wall));
}

// 吸着中は壁に貼り付いたまま、平たく潰れている
void test_stuck_is_flattened_against_wall() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 3.0f;
    in.dt = 0.033f;
    squeeze_update(s, f, in);
    TEST_ASSERT_TRUE(squeeze_scale_x(s) < 1.0f);  // 壁の法線方向に潰れる
    TEST_ASSERT_TRUE(squeeze_scale_y(s) > 1.0f);  // 壁に沿って広がる
    // 逆向きに傾けても、貼り付いている間は動かない
    advance(s, f, -1.0f, 0.0f, 0.0f, 10);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, f.w - f.radius, s.x);
}

// 吸着は kSqStickSec 経過で剥がれ、その後は重力で動き出す
void test_stick_expires_and_falls() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 3.0f;
    in.dt = 0.033f;
    squeeze_update(s, f, in);
    TEST_ASSERT_EQUAL(static_cast<int>(SqueezeWall::Right), static_cast<int>(s.wall));

    // 吸着時間ぶんだけ無重力で進める → 剥がれる
    const int frames = static_cast<int>(kSqStickSec / 0.033f) + 2;
    advance(s, f, 0.0f, 0.0f, 1.0f, frames);
    TEST_ASSERT_EQUAL(static_cast<int>(SqueezeWall::None), static_cast<int>(s.wall));

    // 剥がれた後は下向きの重力で落ちる
    advance(s, f, 0.0f, 1.0f, 0.0f, 20);
    TEST_ASSERT_TRUE(s.y > 120.0f);
    TEST_ASSERT_TRUE(inside(s, f));
}

// 剥がれた直後は壁から内側へ離れる（貼り付いたまま再衝突を繰り返さない）
void test_peel_pushes_away_from_wall() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 3.0f;
    in.dt = 0.033f;
    squeeze_update(s, f, in);
    const int frames = static_cast<int>(kSqStickSec / 0.033f) + 2;
    advance(s, f, 0.0f, 0.0f, 1.0f, frames);
    TEST_ASSERT_TRUE(s.vx < 0.0f);  // 右壁から左（内側）へ
    advance(s, f, 0.0f, 0.0f, 1.0f, 5);
    TEST_ASSERT_TRUE(s.x < f.w - f.radius);
}

// dt が跳ねても（描画が詰まっても）壁をすり抜けない
void test_large_dt_does_not_tunnel() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 5.0f;
    in.dt = 5.0f;  // 5秒ぶんが一気に来た異常ケース
    for (int i = 0; i < 20; ++i) squeeze_update(s, f, in);
    TEST_ASSERT_TRUE(inside(s, f));
}

// 速度は上限を超えない
void test_speed_is_capped() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    // 上限を大きく超える速度を直接与える（傾けだけでは終端速度が上限に届かないため）
    s.vx = 99999.0f;
    s.vy = 99999.0f;
    advance(s, f, 0.0f, 0.0f, 1.0f, 1);
    const float sp = std::sqrt(s.vx * s.vx + s.vy * s.vy);
    TEST_ASSERT_TRUE(sp <= kSqMaxSpeed + 0.01f);
    TEST_ASSERT_TRUE(inside(s, f));
}

// シェイク強度は 0..1 に収まり、静止すると冷める（演出値の健全性）
void test_shake_level_decays() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 3.0f;
    in.dt = 0.033f;
    squeeze_update(s, f, in);
    TEST_ASSERT_TRUE(s.shake > 0.5f);
    TEST_ASSERT_TRUE(s.shake <= 1.0f);
    advance(s, f, 0.0f, 0.0f, 1.0f, 120);
    TEST_ASSERT_TRUE(s.shake < 0.05f);
}

// 不応期があるので、1回の振り（数フレーム続く）で何度もキックが入らない
void test_shake_cooldown_limits_repeat_kicks() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 1.8f;  // ズレ 0.8 → キック閾値は超えるが吸着閾値には届かない
    in.dt = 0.010f;
    squeeze_update(s, f, in);
    const float after_first = s.vx;
    squeeze_update(s, f, in);  // 不応期中なのでキックは入らない（重力ぶんだけ増える）
    const float grav_only = in.gx * kSqGravity * in.dt;
    TEST_ASSERT_TRUE(s.vx - after_first < grav_only * 1.5f);
}

// dt=0 のフレーム（同じ時刻で2回呼ばれた等）は状態を変えない
void test_zero_dt_is_noop() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    advance(s, f, 1.0f, 0.0f, 0.0f, 5);  // まず動かしておく
    const float x = s.x, y = s.y, vx = s.vx, vy = s.vy;
    SqueezeInput in;
    in.gx = 1.0f;
    in.dt = 0.0f;
    squeeze_update(s, f, in);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, x, s.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, y, s.y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, vx, s.vx);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, vy, s.vy);
}

// 負の dt（時刻が巻き戻った）でも位置が逆流しない（0 に丸まる）
void test_negative_dt_does_not_rewind() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    advance(s, f, 1.0f, 0.0f, 0.0f, 5);
    const float x = s.x;
    SqueezeInput in;
    in.gx = 1.0f;
    in.dt = -1.0f;
    squeeze_update(s, f, in);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, x, s.x);
    TEST_ASSERT_TRUE(inside(s, f));
}

// 非有限な入力（センサ異常）は捨てられ、状態が NaN に汚染されない
void test_non_finite_input_is_rejected() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput bad;
    bad.gx = std::nanf("");
    bad.gy = 1.0f;
    bad.dt = 0.033f;
    squeeze_update(s, f, bad);
    TEST_ASSERT_FALSE(std::isnan(s.x));
    TEST_ASSERT_FALSE(std::isnan(s.vx));
    // 汚染されていないので、以後の正常な入力にちゃんと反応する
    advance(s, f, 1.0f, 0.0f, 0.0f, 5);
    TEST_ASSERT_TRUE(s.x > 160.0f);
    TEST_ASSERT_TRUE(inside(s, f));
}

// 吸着中でも、強く振れば別の壁へ貼り替わる
void test_restick_moves_to_another_wall() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 3.0f;
    in.dt = 0.033f;
    squeeze_update(s, f, in);
    TEST_ASSERT_EQUAL(static_cast<int>(SqueezeWall::Right), static_cast<int>(s.wall));
    // 不応期を明けてから逆向きへ強く振る
    advance(s, f, 0.0f, 0.0f, 1.0f, 10);
    in.gx = -3.0f;
    squeeze_update(s, f, in);
    TEST_ASSERT_EQUAL(static_cast<int>(SqueezeWall::Left), static_cast<int>(s.wall));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, f.radius, s.x);
}

// 剥がれる瞬間に形が飛ばない（張り付き形状から連続してぷるんと戻る）
void test_peel_shape_is_continuous() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 3.0f;
    in.dt = 0.033f;
    squeeze_update(s, f, in);
    const float stuck_sx = squeeze_scale_x(s);
    // 剥がれるまで進める
    SqueezeInput rest;
    rest.gz = 1.0f;
    rest.dt = 0.033f;
    while (s.wall != SqueezeWall::None) squeeze_update(s, f, rest);
    // 剥がれた直後もほぼ同じ潰れ具合から始まる（1コマで丸へ飛ばない）
    TEST_ASSERT_FLOAT_WITHIN(0.06f, stuck_sx, squeeze_scale_x(s));
}

// 不応期が明ければ、続けて振った時にまたキックが入る
void test_kick_resumes_after_cooldown() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.gx = 2.0f;  // ズレ 1.0（キックのみ）
    in.dt = 0.033f;
    squeeze_update(s, f, in);
    const float first = s.vx;
    TEST_ASSERT_TRUE(first > 0.0f);
    // 不応期(0.25s)を無入力で明けてから、もう一度同じ強さで振る
    advance(s, f, 0.0f, 0.0f, 1.0f, 12);
    const float before = s.vx;
    squeeze_update(s, f, in);
    TEST_ASSERT_TRUE(s.vx - before > kSqKickSpeed * 0.5f);
}

// 斜めに弱く振った時、画面内成分が閾値に届かなければ吸着しない
void test_stick_needs_enough_in_plane_component() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    // 画面内成分は kSqStickMinPlane 未満、合力の大きさは吸着閾値を超える
    in.gx = kSqStickMinPlane * 0.5f;
    in.gy = kSqStickMinPlane * 0.5f;
    in.gz = 3.0f;
    in.dt = 0.033f;
    squeeze_update(s, f, in);
    TEST_ASSERT_EQUAL(static_cast<int>(SqueezeWall::None), static_cast<int>(s.wall));
}

// 半径倍率は常に正（描画側が負の半径を渡されない）
void test_scales_stay_positive() {
    const SqueezeField f = field();
    SqueezeState s;
    squeeze_reset(s, f);
    SqueezeInput in;
    in.dt = 0.033f;
    // 壁への叩きつけと自由運動を長時間混ぜ、あらゆる位相を通す
    for (int i = 0; i < 600; ++i) {
        in.gx = (i % 7 == 0) ? 3.0f : ((i % 3 == 0) ? -1.0f : 1.0f);
        in.gy = (i % 5 == 0) ? -3.0f : 0.5f;
        squeeze_update(s, f, in);
        TEST_ASSERT_TRUE(squeeze_scale_x(s) > 0.0f);
        TEST_ASSERT_TRUE(squeeze_scale_y(s) > 0.0f);
        TEST_ASSERT_TRUE(inside(s, f));
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_reset_is_centered_and_idle);
    RUN_TEST(test_at_rest_flat_keeps_position);
    RUN_TEST(test_tilt_right_moves_right);
    RUN_TEST(test_tilt_up_moves_up);
    RUN_TEST(test_never_escapes_field);
    RUN_TEST(test_wall_bounces_and_damps);
    RUN_TEST(test_wall_hit_deforms);
    RUN_TEST(test_deform_returns_to_round);
    RUN_TEST(test_tilt_alone_never_sticks);
    RUN_TEST(test_weak_shake_does_not_stick);
    RUN_TEST(test_strong_shake_sticks_to_right_wall);
    RUN_TEST(test_strong_shake_sticks_to_each_wall);
    RUN_TEST(test_z_only_shake_does_not_stick);
    RUN_TEST(test_stuck_is_flattened_against_wall);
    RUN_TEST(test_stick_expires_and_falls);
    RUN_TEST(test_peel_pushes_away_from_wall);
    RUN_TEST(test_large_dt_does_not_tunnel);
    RUN_TEST(test_speed_is_capped);
    RUN_TEST(test_shake_level_decays);
    RUN_TEST(test_shake_cooldown_limits_repeat_kicks);
    RUN_TEST(test_zero_dt_is_noop);
    RUN_TEST(test_negative_dt_does_not_rewind);
    RUN_TEST(test_non_finite_input_is_rejected);
    RUN_TEST(test_restick_moves_to_another_wall);
    RUN_TEST(test_peel_shape_is_continuous);
    RUN_TEST(test_kick_resumes_after_cooldown);
    RUN_TEST(test_stick_needs_enough_in_plane_component);
    RUN_TEST(test_scales_stay_positive);
    return UNITY_END();
}
