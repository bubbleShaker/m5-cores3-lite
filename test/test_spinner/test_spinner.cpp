#include <unity.h>

#include <cmath>

#include "spinner.h"

void setUp(void) {}
void tearDown(void) {}

// 一定の入力で n フレーム進める（1フレーム 33ms ≒ 30fps）。
static void advance(SpinnerState& s, float omega_dev, bool held, bool braking, int n) {
    SpinnerInput in;
    in.omega_dev = omega_dev;
    in.held      = held;
    in.braking   = braking;
    in.dt        = 0.033f;
    for (int i = 0; i < n; ++i) spinner_update(s, in);
}

// 「ハブを押さえて本体を1秒回す」＝実機で言う "はじいて回した" 直後の状態を作る。
static SpinnerState spun_up(float omega_dev = 900.0f) {
    SpinnerState s;
    spinner_reset(s);
    advance(s, omega_dev, /*held=*/true, /*braking=*/false, 30);
    return s;
}

// ───────── 初期状態 ─────────

void test_reset_is_idle() {
    SpinnerState s;
    s.angle = 123.0f;
    s.omega = 456.0f;
    s.turns = 7.0f;
    spinner_reset(s);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.angle);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.omega);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.turns);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.peak_omega);
}

// ───────── 操作の要（Issue #243 の仕様） ─────────

// ハブを押さえていなければ、本体をいくら回してもスピナーは回らない。
// 「中央を押さえてから弾く」という操作に意味を持たせるための性質。
void test_device_spin_without_hold_does_nothing() {
    SpinnerState s;
    spinner_reset(s);
    advance(s, 900.0f, /*held=*/false, /*braking=*/false, 30);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.omega);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.angle);
}

// ハブを押さえて本体を回すと、スピナーが本体の回転に連動して回る。
// カップリングの時定数は 40ms なので、数フレームで目標付近まで到達する。
void test_hold_and_spin_couples_to_device() {
    SpinnerState s;
    spinner_reset(s);
    advance(s, 900.0f, /*held=*/true, /*braking=*/false, 10);
    // 摩擦のぶん目標をわずかに下回るが、9割は乗っている。
    TEST_ASSERT_TRUE(s.omega > 900.0f * 0.9f);
    TEST_ASSERT_TRUE(s.omega <= 900.0f);
}

// 本体を逆向きに回せば、スピナーも逆向きへ引かれる（＝逆回しで減速し、やがて逆転する）。
void test_reverse_device_spin_reverses_spinner() {
    SpinnerState s = spun_up(+900.0f);
    TEST_ASSERT_TRUE(s.omega > 0.0f);
    advance(s, -900.0f, /*held=*/true, /*braking=*/false, 10);
    TEST_ASSERT_TRUE(s.omega < 0.0f);
}

// ★モデルの肝★ 弾き終わって本体が止まっても、スピナーは慣性で回り続ける。
// ハブを押さえたままでも止まらないこと（軸を持っているだけでベアリングは自由に回る）。
void test_keeps_spinning_after_device_stops() {
    SpinnerState s = spun_up(900.0f);
    const float after_flick = s.omega;
    // 本体は静止（omega_dev=0）だが指はハブに乗せたまま、2秒経過。
    advance(s, 0.0f, /*held=*/true, /*braking=*/false, 60);
    TEST_ASSERT_TRUE(s.omega > 0.0f);              // 止まっていない
    TEST_ASSERT_TRUE(s.omega > after_flick * 0.5f);  // 摩擦は緩やか（2秒で半分以上残る）
    TEST_ASSERT_TRUE(s.omega < after_flick);         // が、確かに減っている
}

// 手ブレ程度（kSpDriveMin 未満）の本体の揺れではカップリングが効かず、慣性を殺さない。
void test_small_device_jitter_does_not_brake() {
    SpinnerState s = spun_up(900.0f);
    const float before = s.omega;
    advance(s, kSpDriveMin * 0.5f, /*held=*/true, /*braking=*/false, 30);
    // 摩擦ぶん(1秒で約20%)しか減らない＝目標 45deg/s へ引き寄せられていない。
    TEST_ASSERT_TRUE(s.omega > before * 0.7f);
}

// スピン部を押さえると素早く止まる（0.3秒あれば完全停止）。
void test_braking_stops_quickly() {
    SpinnerState s = spun_up(2000.0f);
    advance(s, 0.0f, /*held=*/false, /*braking=*/true, 10);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.omega);
}

// ブレーキは held より優先する。軸を持ったまま本体を回していても、ローブを触れば止まる。
void test_braking_wins_over_hold() {
    SpinnerState s = spun_up(2000.0f);
    advance(s, 2000.0f, /*held=*/true, /*braking=*/true, 10);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.omega);
}

// 放っておけば摩擦だけでいつか完全に止まる（指数減衰の漸近で止まりきらない、を潰す）。
void test_friction_eventually_stops() {
    SpinnerState s = spun_up(2880.0f);
    advance(s, 0.0f, /*held=*/false, /*braking=*/false, 30 * 60);  // 60秒
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.omega);
}

// ただし「すぐには」止まらない。10秒後もまだ回っている＝触って止める遊びが成立する。
void test_still_spinning_after_ten_seconds() {
    SpinnerState s = spun_up(2000.0f);
    advance(s, 0.0f, /*held=*/false, /*braking=*/false, 30 * 10);
    TEST_ASSERT_TRUE(s.omega > kSpStopEps);
}

// カップリングが効く下限はちょうど kSpDriveMin から（仕様は >=）。
// 境界のすぐ下では効かず、境界ちょうどでは効く。
void test_drive_min_is_inclusive_boundary() {
    SpinnerState below;
    spinner_reset(below);
    advance(below, kSpDriveMin * 0.999f, /*held=*/true, /*braking=*/false, 10);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, below.omega);  // 完全停止扱い（kSpStopEps 未満）

    SpinnerState at;
    spinner_reset(at);
    advance(at, kSpDriveMin, /*held=*/true, /*braking=*/false, 10);
    TEST_ASSERT_TRUE(at.omega > kSpStopEps);
}

// ───────── 不変条件と堅牢性 ─────────

// ★契約★ omega が正なら角度は増える（＝画面上の時計回りが正）。
// これが無いと、符号を逆に実装しても他の全テストが通ってしまう。
void test_positive_omega_increases_angle() {
    SpinnerState s;
    spinner_reset(s);
    s.omega = 90.0f;  // 4秒で1回転。1フレーム(33ms)では 3度弱しか進まず折り返さない
    SpinnerInput in;
    in.dt = 0.033f;
    spinner_update(s, in);
    TEST_ASSERT_TRUE(s.angle > 0.0f && s.angle < 90.0f);

    // 負なら 360 側から戻る（0 を下回った角度は畳まれる）。
    spinner_reset(s);
    s.omega = -90.0f;
    spinner_update(s, in);
    TEST_ASSERT_TRUE(s.angle > 270.0f && s.angle < 360.0f);
}

// 角度は常に [0, 360) に正規化されている（描画側が畳み直さなくてよい）。
void test_angle_always_normalized() {
    SpinnerState s;
    spinner_reset(s);
    for (int i = 0; i < 300; ++i) {
        advance(s, 2000.0f, /*held=*/true, /*braking=*/false, 1);
        TEST_ASSERT_TRUE(s.angle >= 0.0f && s.angle < 360.0f);
    }
    // 逆回しでも同じ（fmod が負を返す経路）。
    for (int i = 0; i < 300; ++i) {
        advance(s, -2000.0f, /*held=*/true, /*braking=*/false, 1);
        TEST_ASSERT_TRUE(s.angle >= 0.0f && s.angle < 360.0f);
    }
}

// 本体をどれだけ速く回しても角速度は上限で頭打ちになる（コマ送りで逆回転に見えるのを防ぐ）。
void test_omega_is_clamped() {
    SpinnerState s;
    spinner_reset(s);
    advance(s, 99999.0f, /*held=*/true, /*braking=*/false, 30);
    TEST_ASSERT_TRUE(std::fabs(s.omega) <= kSpMaxOmega + 0.001f);
}

// 非有限値(NaN/Inf)が来たフレームは丸ごと捨て、状態を汚さない（表示用の集計も含む）。
void test_non_finite_input_is_ignored() {
    SpinnerState s = spun_up(900.0f);
    const float omega = s.omega;
    const float angle = s.angle;
    const float turns = s.turns;
    const float peak  = s.peak_omega;

    SpinnerInput bad;
    bad.omega_dev = NAN;
    bad.held      = true;
    bad.dt        = 0.033f;
    spinner_update(s, bad);
    bad.omega_dev = INFINITY;
    spinner_update(s, bad);
    bad.omega_dev = 900.0f;
    bad.dt        = NAN;
    spinner_update(s, bad);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, omega, s.omega);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, angle, s.angle);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, turns, s.turns);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, peak, s.peak_omega);
}

// dt が 0 や負でも状態が動かない（時計の巻き戻りで角度が戻らない）。
void test_zero_or_negative_dt_is_ignored() {
    SpinnerState s = spun_up(900.0f);
    const float angle = s.angle;
    const float turns = s.turns;
    advance(s, 900.0f, /*held=*/true, /*braking=*/false, 0);

    SpinnerInput in;
    in.omega_dev = 900.0f;
    in.held      = true;
    in.dt        = 0.0f;
    spinner_update(s, in);
    in.dt = -1.0f;
    spinner_update(s, in);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, angle, s.angle);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, turns, s.turns);
}

// dt のクランプは角度だけでなく減衰にも効く（巨大 dt の1フレームで摩擦が効きすぎない）。
void test_huge_dt_clamps_decay_too() {
    SpinnerState s;
    spinner_reset(s);
    s.omega = 1000.0f;

    SpinnerInput in;
    in.dt = 100.0f;  // クランプが無ければ摩擦で完全に消える長さ
    spinner_update(s, in);
    // kSpMaxDt(0.05s) ぶんの摩擦しか掛からない＝ほぼ減らない。
    TEST_ASSERT_TRUE(s.omega > 900.0f);
}

// 描画が詰まって dt が跳ねても、1フレームで進む角度は上限 dt ぶんに抑えられる。
void test_huge_dt_is_clamped() {
    SpinnerState s;
    spinner_reset(s);
    s.omega = 360.0f;  // 1回転/秒

    SpinnerInput in;
    in.omega_dev = 0.0f;
    in.dt        = 10.0f;  // 10秒ぶんの遅れ
    spinner_update(s, in);
    // クランプが無ければ 3600 度＝10回転ぶん進む。実際は kSpMaxDt(0.05s) ぶん＝18度以内。
    TEST_ASSERT_TRUE(s.angle <= 360.0f * kSpMaxDt + 0.001f);
}

// ───────── 表示用の集計 ─────────

void test_rpm_conversion() {
    SpinnerState s;
    spinner_reset(s);
    s.omega = 360.0f;  // 1回転/秒 = 60rpm
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, spinner_rpm(s));
    s.omega = -360.0f;  // 向きは問わない
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, spinner_rpm(s));
}

// 累計回転数は回した分だけ増え、逆回転でも減らない。
void test_turns_accumulate_both_directions() {
    SpinnerState s;
    spinner_reset(s);
    advance(s, 900.0f, /*held=*/true, /*braking=*/false, 30);
    const float forward = s.turns;
    TEST_ASSERT_TRUE(forward > 0.0f);
    advance(s, -900.0f, /*held=*/true, /*braking=*/false, 30);
    TEST_ASSERT_TRUE(s.turns > forward);
}

// 最高角速度は「最大値」を保つ。減速しても下がらない。
void test_peak_holds_maximum_while_slowing() {
    SpinnerState s = spun_up(2000.0f);
    const float peak = s.peak_omega;
    TEST_ASSERT_TRUE(peak > 1000.0f);
    advance(s, 0.0f, /*held=*/false, /*braking=*/false, 30 * 3);  // 3秒かけて減速
    TEST_ASSERT_TRUE(s.omega < peak);                             // 実際に遅くなっている
    TEST_ASSERT_FLOAT_WITHIN(0.001f, peak, s.peak_omega);         // が、最高値は据え置き
}

// 最高角速度は完全停止しても残る。止まった直後こそベストを読みたいタイミングなので、
// 停止と同時に消してはいけない。
void test_peak_survives_full_stop() {
    SpinnerState s = spun_up(2000.0f);
    const float peak = s.peak_omega;
    TEST_ASSERT_TRUE(peak > 1000.0f);
    advance(s, 0.0f, /*held=*/false, /*braking=*/true, 20);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.omega);      // 止まっているが
    TEST_ASSERT_FLOAT_WITHIN(0.001f, peak, s.peak_omega);  // 記録は残る
}

// 次に回し始めた瞬間に最高記録を取り直す（1回しごとのベストになる）。
void test_peak_resets_when_spinning_starts_again() {
    SpinnerState s = spun_up(2000.0f);
    advance(s, 0.0f, /*held=*/false, /*braking=*/true, 20);
    TEST_ASSERT_TRUE(s.peak_omega > 1000.0f);  // 前回の記録が残っている

    // ゆっくり回し直すと、前回の記録ではなく今回の値になる。
    advance(s, 300.0f, /*held=*/true, /*braking=*/false, 10);
    TEST_ASSERT_TRUE(s.peak_omega > 0.0f);
    TEST_ASSERT_TRUE(s.peak_omega < 400.0f);
}

void test_peak_rpm_conversion() {
    SpinnerState s;
    spinner_reset(s);
    s.peak_omega = 720.0f;  // 2回転/秒 = 120rpm
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, spinner_peak_rpm(s));
}

// ───────── 押下ラッチ（短タップで回転を消してよいかの判定） ─────────

// 押下開始でスピナーに触れたらラッチが立ち、押下中は維持される。
void test_latch_sets_on_body_and_holds() {
    bool latched = false;
    latched = spinner_press_latch(latched, /*touching=*/true, /*on_body=*/true);
    TEST_ASSERT_TRUE(latched);
    // 指がわずかにずれて一瞬スピナーから外れても落ちない（押下が続いている限り維持）。
    latched = spinner_press_latch(latched, /*touching=*/true, /*on_body=*/false);
    TEST_ASSERT_TRUE(latched);
}

// 指を離したらクリアされ、次の押下に持ち越さない。
void test_latch_clears_when_released() {
    bool latched = spinner_press_latch(false, true, true);
    TEST_ASSERT_TRUE(latched);
    latched = spinner_press_latch(latched, /*touching=*/false, /*on_body=*/false);
    TEST_ASSERT_FALSE(latched);
}

// スピナーの外だけを触った押下ではラッチが立たない（＝この短タップは仕切り直しになる）。
void test_latch_stays_clear_outside() {
    bool latched = false;
    for (int i = 0; i < 10; ++i) {
        latched = spinner_press_latch(latched, /*touching=*/true, /*on_body=*/false);
    }
    TEST_ASSERT_FALSE(latched);
}

// 押下の途中からスピナーに触れた場合も立つ（外から中へ指をずらした操作）。
void test_latch_sets_midway_through_press() {
    bool latched = spinner_press_latch(false, true, false);
    TEST_ASSERT_FALSE(latched);
    latched = spinner_press_latch(latched, true, true);
    TEST_ASSERT_TRUE(latched);
}

// ───────── 触れた場所の区分 ─────────

void test_zone_center_is_hub() {
    TEST_ASSERT_EQUAL(static_cast<int>(SpinnerZone::Hub),
                      static_cast<int>(spinner_zone(0.0f, 0.0f)));
    // 斜めでも距離で決まる（正方形ではなく円で判定している）。
    TEST_ASSERT_EQUAL(static_cast<int>(SpinnerZone::Hub),
                      static_cast<int>(spinner_zone(20.0f, 20.0f)));  // 距離 28.3
}

void test_zone_lobes_are_body() {
    TEST_ASSERT_EQUAL(static_cast<int>(SpinnerZone::Body),
                      static_cast<int>(spinner_zone(70.0f, 0.0f)));
    TEST_ASSERT_EQUAL(static_cast<int>(SpinnerZone::Body),
                      static_cast<int>(spinner_zone(0.0f, -100.0f)));
}

void test_zone_far_is_outside() {
    // 画面の隅（320x240 の角は中心から約200px）は必ず外側＝長押しでメニューへ戻せる。
    TEST_ASSERT_EQUAL(static_cast<int>(SpinnerZone::Outside),
                      static_cast<int>(spinner_zone(160.0f, 120.0f)));
}

// 境界はどちらも「以内」を含む（内側が優先）。
void test_zone_boundaries_are_inclusive() {
    TEST_ASSERT_EQUAL(static_cast<int>(SpinnerZone::Hub),
                      static_cast<int>(spinner_zone(kSpHubTouchR, 0.0f)));
    TEST_ASSERT_EQUAL(static_cast<int>(SpinnerZone::Body),
                      static_cast<int>(spinner_zone(kSpHubTouchR + 0.1f, 0.0f)));
    TEST_ASSERT_EQUAL(static_cast<int>(SpinnerZone::Body),
                      static_cast<int>(spinner_zone(kSpBodyTouchR, 0.0f)));
    TEST_ASSERT_EQUAL(static_cast<int>(SpinnerZone::Outside),
                      static_cast<int>(spinner_zone(kSpBodyTouchR + 0.1f, 0.0f)));
}

// 壊れた座標は Outside 扱い（誤って「軸を持った」ことにしない安全側）。
void test_zone_non_finite_is_outside() {
    TEST_ASSERT_EQUAL(static_cast<int>(SpinnerZone::Outside),
                      static_cast<int>(spinner_zone(NAN, 0.0f)));
    TEST_ASSERT_EQUAL(static_cast<int>(SpinnerZone::Outside),
                      static_cast<int>(spinner_zone(0.0f, INFINITY)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_reset_is_idle);
    RUN_TEST(test_device_spin_without_hold_does_nothing);
    RUN_TEST(test_hold_and_spin_couples_to_device);
    RUN_TEST(test_reverse_device_spin_reverses_spinner);
    RUN_TEST(test_keeps_spinning_after_device_stops);
    RUN_TEST(test_small_device_jitter_does_not_brake);
    RUN_TEST(test_braking_stops_quickly);
    RUN_TEST(test_braking_wins_over_hold);
    RUN_TEST(test_friction_eventually_stops);
    RUN_TEST(test_still_spinning_after_ten_seconds);
    RUN_TEST(test_drive_min_is_inclusive_boundary);
    RUN_TEST(test_positive_omega_increases_angle);
    RUN_TEST(test_angle_always_normalized);
    RUN_TEST(test_omega_is_clamped);
    RUN_TEST(test_non_finite_input_is_ignored);
    RUN_TEST(test_zero_or_negative_dt_is_ignored);
    RUN_TEST(test_huge_dt_is_clamped);
    RUN_TEST(test_huge_dt_clamps_decay_too);
    RUN_TEST(test_rpm_conversion);
    RUN_TEST(test_turns_accumulate_both_directions);
    RUN_TEST(test_peak_holds_maximum_while_slowing);
    RUN_TEST(test_peak_survives_full_stop);
    RUN_TEST(test_peak_resets_when_spinning_starts_again);
    RUN_TEST(test_peak_rpm_conversion);
    RUN_TEST(test_latch_sets_on_body_and_holds);
    RUN_TEST(test_latch_clears_when_released);
    RUN_TEST(test_latch_stays_clear_outside);
    RUN_TEST(test_latch_sets_midway_through_press);
    RUN_TEST(test_zone_center_is_hub);
    RUN_TEST(test_zone_lobes_are_body);
    RUN_TEST(test_zone_far_is_outside);
    RUN_TEST(test_zone_boundaries_are_inclusive);
    RUN_TEST(test_zone_non_finite_is_outside);
    return UNITY_END();
}
