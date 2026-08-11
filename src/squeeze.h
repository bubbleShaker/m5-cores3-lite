#pragma once

#include <cstdint>

// スクイーズ（ぷにぷに玉）シーンの純粋物理層（Issue #240）。
// M5Unified・millis()・描画には一切依存しない。「引っ張られる向き」と「経過時間」だけを
// 受け取り、玉の位置・速度・変形・吸着状態を進める。pacman.h / gem3d.h と同じ思想で、
// 調整点の多い物理（反発係数・シェイク閾値）を PC 上の native テストで詰められるようにする。
//
// ■ 座標系
//   画面座標に合わせる。左上が (0,0)、右へ +x、下へ +y。単位は px。
//
// ■ 入力の約束（ここが実機との境界）
//   SqueezeInput の (gx, gy, gz) は **玉が引っ張られる向き**を G 単位で表す。
//   加速度センサの生値ではない。センサは「静止時に上向きの軸が +1G を示す」ため、
//   生値の符号を反転して渡すのは呼び出し側（main.cpp）の責務とする。
//   軸の入れ替え・符号は基板の向きで変わり、この層で吸収すると嘘になるため。

// 吸着している壁。None は「どこにも貼り付いていない＝自由落下中」。
enum class SqueezeWall : uint8_t { None, Left, Right, Top, Bottom };

// ───────── 物理定数（実機で体感を詰める時はここを触る） ─────────

// 1G あたりの加速度(px/s^2)。900 だと 320px の画面をおよそ 0.85 秒で横断する。
constexpr float kSqGravity = 900.0f;
// 空気抵抗（1秒あたりに失う速度の割合）。無いと永久に往復して落ち着かない。
constexpr float kSqDrag = 1.1f;
// 壁の反発係数。1.0 で完全弾性、0 で貼り付き。0.55 は「軽く弾む」体感。
constexpr float kSqBounce = 0.55f;
// 速度の上限(px/s)。1フレームで画面を飛び越えて壁をすり抜けるのを防ぐ。
constexpr float kSqMaxSpeed = 1400.0f;

// シェイク判定の閾値（|引っ張られる力| の 1G からのズレ・G 単位）。
// 静止・傾けだけでは大きさが 1G のままなので 0 に近く、誤発火しない。
constexpr float kSqShakeKick  = 0.70f;  // これ以上で「速度キック」
constexpr float kSqShakeStick = 1.50f;  // これ以上で「壁に吸着」
// シェイクで与える速度(px/s)。ズレの大きさに比例して掛ける。
constexpr float kSqKickSpeed = 900.0f;
// シェイク検出の不応期(秒)。1回の振りは数フレームにまたがるため、これを置かないと
// 同じ振りで何度もキックが入って挙動が暴れる。
constexpr float kSqShakeCoolSec = 0.25f;
// 吸着方向を決めるのに必要な画面内成分の最小値(G)。画面に垂直(z)にだけ振った時に
// 上下左右のどこかへ勝手に貼り付くのを防ぐ。
constexpr float kSqStickMinPlane = 0.35f;
// 吸着が続く時間(秒)。経過すると剥がれて落ち始める。
constexpr float kSqStickSec = 1.30f;

// 変形（減衰振動）のパラメータ。ぷにぷに感はこの3つで決まる。
constexpr float kSqWobbleHz    = 5.5f;   // 揺れの速さ(Hz)
constexpr float kSqWobbleDecay = 3.2f;   // 減衰の速さ(1/s)。大きいほど早く収まる
constexpr float kSqWobbleMax   = 0.45f;  // 最大変形量（半径の±45%）
// 変形の縦横の連動。潰れた分だけ直交方向へ膨らませて「体積が保たれた」ように見せる。
constexpr float kSqWobbleCross = 0.75f;

// 吸着中の張り付き形状。壁の法線方向に潰れ、壁に沿う方向へ広がる。
constexpr float kSqStickFlatN = 0.55f;  // 法線方向の倍率（潰れる）
constexpr float kSqStickFlatT = 1.35f;  // 接線方向の倍率（広がる）
// 剥がれた瞬間に壁から離す初速(px/s)。0 だと壁に埋まったまま再衝突を繰り返す。
constexpr float kSqPeelSpeed = 90.0f;

// 1フレームで進める時間の上限(秒)。描画が詰まって dt が跳ねた時に、
// 位置が一気に飛んで壁を突き抜けるのを防ぐ。
constexpr float kSqMaxDt = 0.05f;

// ───────── 状態と入出力 ─────────

// 玉が動ける矩形と大きさ。中心は [radius, w-radius] × [radius, h-radius] に収まる。
struct SqueezeField {
    float w      = 320.0f;  // フィールド幅(px)
    float h      = 240.0f;  // フィールド高(px)
    float radius = 34.0f;   // 玉の基準半径(px)
};

// 玉の状態。呼び出し側が1個保持して毎フレーム squeeze_update に渡す。
struct SqueezeState {
    float x  = 0.0f;  // 中心座標
    float y  = 0.0f;
    float vx = 0.0f;  // 速度(px/s)
    float vy = 0.0f;

    // 変形（減衰振動）。衝突やシェイクのたびに amp を立て直し、t を 0 に戻す。
    float wob_amp   = 0.0f;   // 振幅 0..1（衝突の強さ）
    float wob_t     = 0.0f;   // 直近の衝突からの経過秒
    bool  wob_horiz = true;   // true=横に潰れる（左右の壁）／false=縦に潰れる

    // 吸着。
    SqueezeWall wall       = SqueezeWall::None;
    float       stuck_left = 0.0f;  // 吸着の残り時間(秒)

    // シェイク検出。
    float shake     = 0.0f;  // 直近のシェイク強度 0..1（減衰する・演出用）
    float cool_left = 0.0f;  // 不応期の残り時間(秒)
};

// 1フレーム分の入力。
struct SqueezeInput {
    float gx = 0.0f;  // 玉が引っ張られる向き（画面座標・G単位）。センサ生値ではない
    float gy = 0.0f;
    float gz = 0.0f;  // 画面に垂直な成分。シェイクの大きさ判定にのみ使う
    float dt = 0.0f;  // 前フレームからの経過秒
};

// 玉をフィールド中央に置き、速度・変形・吸着を全て初期化する。
void squeeze_reset(SqueezeState& s, const SqueezeField& f);

// 物理を dt だけ進める。dt は内部で [0, kSqMaxDt] にクランプされる。
// 呼び出し後、玉の中心は必ずフィールド内に収まっている（不変条件）。
void squeeze_update(SqueezeState& s, const SqueezeField& f, const SqueezeInput& in);

// 描画用の半径倍率。基準半径 f.radius に掛けて使う。無変形なら 1.0。
// 吸着中は張り付き形状、それ以外は減衰振動の現在値を返す。
float squeeze_scale_x(const SqueezeState& s);
float squeeze_scale_y(const SqueezeState& s);
