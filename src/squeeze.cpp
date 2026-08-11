#include "squeeze.h"

#include <cmath>

namespace {

float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// 減衰振動の現在の変形量。+ が「wob_horiz の軸方向に潰れている」。
// exp で減衰しながら cos で往復する＝ぶつかった瞬間が一番潰れ、揺り戻しながら丸に戻る。
float wobble_amount(const SqueezeState& s) {
    if (s.wob_amp <= 0.0f) return 0.0f;
    const float decay = std::exp(-kSqWobbleDecay * s.wob_t);
    const float osc   = std::cos(2.0f * 3.14159265f * kSqWobbleHz * s.wob_t);
    return kSqWobbleMax * s.wob_amp * decay * osc;
}

// 衝突・シェイクを受けて変形を新しく立て直す。
// 既に潰れている最中でも、より強い衝撃なら上書きする（弱い衝撃で揺れが打ち消されない）。
void kick_wobble(SqueezeState& s, float amp, bool horiz) {
    const float a = clampf(amp, 0.0f, 1.0f);
    if (a <= 0.0f) return;
    // 現在の残り振幅より弱ければ無視する。強ければ位相ごと最初からやり直す。
    const float remain = s.wob_amp * std::exp(-kSqWobbleDecay * s.wob_t);
    if (a < remain) return;
    s.wob_amp   = a;
    s.wob_t     = 0.0f;
    s.wob_horiz = horiz;
}

// 壁に当たった時の処理。位置を面まで戻し、法線速度を反転して減衰させ、変形を起こす。
// 戻り値は「実際に当たったか」。
bool bounce_axis(float& pos, float& vel, float lo, float hi, SqueezeState& s, bool horiz) {
    float hit_speed = 0.0f;
    if (pos < lo) {
        pos       = lo;
        hit_speed = -vel;
        vel       = -vel * kSqBounce;
    } else if (pos > hi) {
        pos       = hi;
        hit_speed = vel;
        vel       = -vel * kSqBounce;
    } else {
        return false;
    }
    // 衝突の強さを 0..1 に正規化して変形の振幅にする。速く当たるほど大きく潰れる。
    kick_wobble(s, hit_speed / kSqMaxSpeed, horiz);
    return true;
}

// 吸着先の壁を、引っ張られている向きの支配的な軸から決める。
// 画面内成分が小さい（＝画面に垂直にだけ振った）時は None を返して吸着させない。
SqueezeWall pick_wall(float gx, float gy) {
    const float ax = std::fabs(gx);
    const float ay = std::fabs(gy);
    if (ax < kSqStickMinPlane && ay < kSqStickMinPlane) return SqueezeWall::None;
    if (ax >= ay) return gx > 0.0f ? SqueezeWall::Right : SqueezeWall::Left;
    return gy > 0.0f ? SqueezeWall::Bottom : SqueezeWall::Top;
}

// 指定の壁へ玉を移動させ、貼り付いた状態にする。
void stick_to(SqueezeState& s, const SqueezeField& f, SqueezeWall wall, float strength) {
    const float r = f.radius;
    switch (wall) {
        case SqueezeWall::Left:   s.x = r;         break;
        case SqueezeWall::Right:  s.x = f.w - r;   break;
        case SqueezeWall::Top:    s.y = r;         break;
        case SqueezeWall::Bottom: s.y = f.h - r;   break;
        case SqueezeWall::None:   return;
    }
    s.wall       = wall;
    s.stuck_left = kSqStickSec;
    s.vx         = 0.0f;
    s.vy         = 0.0f;
    const bool horiz = (wall == SqueezeWall::Left || wall == SqueezeWall::Right);
    kick_wobble(s, clampf(strength, 0.0f, 1.0f), horiz);
}

}  // namespace

void squeeze_reset(SqueezeState& s, const SqueezeField& f) {
    s = SqueezeState{};
    s.x = f.w * 0.5f;
    s.y = f.h * 0.5f;
}

void squeeze_update(SqueezeState& s, const SqueezeField& f, const SqueezeInput& in) {
    const float dt = clampf(in.dt, 0.0f, kSqMaxDt);

    // 変形と各種タイマーは、動いていなくても常に時間で進める。
    s.wob_t += dt;
    if (s.cool_left > 0.0f) s.cool_left -= dt;
    // 演出用のシェイク強度は指数的に冷ます（画面のエフェクトが一瞬で消えないように）。
    s.shake -= s.shake * clampf(kSqWobbleDecay * dt, 0.0f, 1.0f);

    // ── シェイク検出 ──
    // 静止・傾けでは合力の大きさは常に 1G なので、1G からのズレがそのまま「振られた量」になる。
    // 生の加速度の向きではなく大きさを見るため、どちら向きに振っても等しく反応する。
    const float mag = std::sqrt(in.gx * in.gx + in.gy * in.gy + in.gz * in.gz);
    const float dev = std::fabs(mag - 1.0f);
    if (dev > s.shake) s.shake = clampf(dev, 0.0f, 1.0f);

    bool shook = false;
    if (dev >= kSqShakeKick && s.cool_left <= 0.0f) {
        shook       = true;
        s.cool_left = kSqShakeCoolSec;
    }

    if (shook && dev >= kSqShakeStick) {
        // 強く振られた: 引っ張られた向きの壁へ叩きつけて貼り付ける。
        const SqueezeWall wall = pick_wall(in.gx, in.gy);
        if (wall != SqueezeWall::None) {
            stick_to(s, f, wall, clampf(dev / kSqShakeStick, 0.0f, 1.0f));
            return;  // 貼り付いた瞬間は物理を進めない（この場で静止している）
        }
    }

    const float rx_lo = f.radius, rx_hi = f.w - f.radius;
    const float ry_lo = f.radius, ry_hi = f.h - f.radius;

    // ── 吸着中 ──
    if (s.wall != SqueezeWall::None) {
        s.stuck_left -= dt;
        if (s.stuck_left > 0.0f) {
            // 貼り付いている間は位置を壁に固定する（傾けても動かない）。
            s.x = clampf(s.x, rx_lo, rx_hi);
            s.y = clampf(s.y, ry_lo, ry_hi);
            return;
        }
        // 剥がれた: 壁から内側へ弾き出してから通常の物理へ戻す。
        // 初速を与えないと壁に貼り付いたまま毎フレーム再衝突する。
        switch (s.wall) {
            case SqueezeWall::Left:   s.vx = +kSqPeelSpeed; break;
            case SqueezeWall::Right:  s.vx = -kSqPeelSpeed; break;
            case SqueezeWall::Top:    s.vy = +kSqPeelSpeed; break;
            case SqueezeWall::Bottom: s.vy = -kSqPeelSpeed; break;
            case SqueezeWall::None:   break;
        }
        s.wall       = SqueezeWall::None;
        s.stuck_left = 0.0f;
    }

    // ── 自由運動 ──
    // 傾き: 引っ張られる向きをそのまま加速度にする。
    s.vx += in.gx * kSqGravity * dt;
    s.vy += in.gy * kSqGravity * dt;

    // 弱〜中程度のシェイク: 吸着はせず速度だけ足す（手の中で暴れる感じ）。
    if (shook) {
        const float plane = std::sqrt(in.gx * in.gx + in.gy * in.gy);
        if (plane > 0.0f) {
            const float k = kSqKickSpeed * clampf(dev, 0.0f, 2.0f);
            s.vx += in.gx / plane * k;
            s.vy += in.gy / plane * k;
            kick_wobble(s, clampf(dev * 0.5f, 0.0f, 1.0f), std::fabs(in.gx) >= std::fabs(in.gy));
        }
    }

    // 空気抵抗。dt が大きい時に係数が 1 を超えて速度が逆流しないようクランプする。
    const float drag = clampf(kSqDrag * dt, 0.0f, 1.0f);
    s.vx -= s.vx * drag;
    s.vy -= s.vy * drag;

    // 速度上限。1フレームの移動量を抑え、壁のすり抜けを構造的に防ぐ。
    const float sp = std::sqrt(s.vx * s.vx + s.vy * s.vy);
    if (sp > kSqMaxSpeed) {
        const float k = kSqMaxSpeed / sp;
        s.vx *= k;
        s.vy *= k;
    }

    s.x += s.vx * dt;
    s.y += s.vy * dt;

    // 壁との衝突。x/y を独立に見るので、角に当たった時は両軸で跳ね返る。
    bounce_axis(s.x, s.vx, rx_lo, rx_hi, s, true);
    bounce_axis(s.y, s.vy, ry_lo, ry_hi, s, false);
}

float squeeze_scale_x(const SqueezeState& s) {
    if (s.wall != SqueezeWall::None) {
        const bool horiz = (s.wall == SqueezeWall::Left || s.wall == SqueezeWall::Right);
        return horiz ? kSqStickFlatN : kSqStickFlatT;
    }
    const float a = wobble_amount(s);
    return s.wob_horiz ? (1.0f - a) : (1.0f + a * kSqWobbleCross);
}

float squeeze_scale_y(const SqueezeState& s) {
    if (s.wall != SqueezeWall::None) {
        const bool horiz = (s.wall == SqueezeWall::Left || s.wall == SqueezeWall::Right);
        return horiz ? kSqStickFlatT : kSqStickFlatN;
    }
    const float a = wobble_amount(s);
    return s.wob_horiz ? (1.0f + a * kSqWobbleCross) : (1.0f - a);
}
