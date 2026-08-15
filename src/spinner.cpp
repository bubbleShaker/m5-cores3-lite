#include "spinner.h"

#include <cmath>

namespace {

// 角度を [0, 360) に畳む。fmod は負の入力で負を返すので、その分を足し戻す。
float wrap_deg(float a) {
    a = std::fmod(a, 360.0f);
    if (a < 0.0f) a += 360.0f;
    return a;
}

float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// 指数減衰を dt 秒ぶん掛ける。rate は 1/s（大きいほど速く減る）。
// フレームレートが揺れても減り方が変わらないよう、dt を掛けるのではなく exp を使う。
float decay(float v, float rate, float dt) {
    return v * std::exp(-rate * dt);
}

bool finite(float v) {
    return std::isfinite(v);
}

}  // namespace

void spinner_reset(SpinnerState& s) {
    s.angle      = 0.0f;
    s.omega      = 0.0f;
    s.turns      = 0.0f;
    s.peak_omega = 0.0f;
}

void spinner_update(SpinnerState& s, const SpinnerInput& in) {
    // 非有限値が1つでも混じったフレームは丸ごと捨てる。IMU の読み取り失敗やゼロ除算が
    // 紛れ込んだ時に、NaN が angle/omega に伝染して以後ずっと描画が壊れるのを防ぐ。
    if (!finite(in.omega_dev) || !finite(in.dt)) return;

    const float dt = clampf(in.dt, 0.0f, kSpMaxDt);
    if (dt <= 0.0f) return;

    if (in.braking) {
        // ブレーキ: スピン部に指が乗っている間は強い減衰で止める。
        // held より優先する。両方に指が乗っている状況（軸を持ったままローブを触る）は
        // 「止めたい」意図の方が明確なため。
        s.omega = decay(s.omega, kSpBrakeDamp, dt);
    } else {
        // 軸受け摩擦。ブレーキ中でなければ常に効く（＝放っておけばいつか止まる）。
        s.omega = decay(s.omega, kSpFriction, dt);

        // ハブを押さえていて、かつ本体が実際に回っている間だけ、本体の回転が伝わる。
        // 下限 kSpDriveMin を置く理由は spinner.h のモデル説明を参照（無いと慣性が死ぬ）。
        if (in.held && std::fabs(in.omega_dev) >= kSpDriveMin) {
            const float target = kSpDriveGain * in.omega_dev;
            // 指数漸近の厳密形。dt を掛ける線形補間だと、フレームが飛んだ時に係数が
            // 1 を超えて目標を行き過ぎ、振動する。
            const float k = 1.0f - std::exp(-kSpCouple * dt);
            s.omega += (target - s.omega) * k;
        }
    }

    // 上限クランプ。1フレームで進み過ぎて回転方向が逆に見えるのを防ぐ（kSpMaxOmega の説明）。
    s.omega = clampf(s.omega, -kSpMaxOmega, kSpMaxOmega);
    // 止まりきらない指数減衰に終わりを作る。
    if (std::fabs(s.omega) < kSpStopEps) s.omega = 0.0f;

    s.angle = wrap_deg(s.angle + s.omega * dt);

    // ── 以下は表示専用の集計。物理には影響しない ──
    const float speed = std::fabs(s.omega);
    s.turns += speed * dt / 360.0f;
    if (speed > s.peak_omega) s.peak_omega = speed;
    // 完全に止まった時だけ最高速をリセットする。次の1回しのベストを独立して見せるため。
    if (s.omega == 0.0f) s.peak_omega = 0.0f;
}

float spinner_rpm(const SpinnerState& s) {
    return std::fabs(s.omega) / 6.0f;  // deg/s → rpm は 60/360 = 1/6
}
