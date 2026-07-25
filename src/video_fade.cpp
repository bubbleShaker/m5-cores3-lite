#include "video_fade.h"

void video_fade_init(VideoFade* f) {
    if (f == nullptr) return;
    f->state    = VideoFadeState::kIdle;
    f->start_ms = 0;
    f->pending  = -1;
    // 初期化直後は「等倍で描き直す必要は無い」。呼び出し側（videoShowSelect）が
    // videoDiscPrepare で絵を作り直す経路なので、余分な複写を1回させない。
    f->settled  = true;
}

namespace {

// 起点からの経過 ms。起点より前の now は 0 扱い（uint32 のアンダーフローで一瞬デタラメな
// 輝度を出さない。videoUpdate の起点ガードと同じ int32 キャストの型）。
uint32_t fade_elapsed(const VideoFade* f, uint32_t now) {
    if (static_cast<int32_t>(now - f->start_ms) < 0) return 0;
    return now - f->start_ms;
}

}  // namespace

void video_fade_request(VideoFade* f, uint32_t now, int target, uint32_t dur_ms) {
    if (f == nullptr) return;
    switch (f->state) {
    case VideoFadeState::kOut:
        // 暗転はやり直さず行き先だけ差し替える。連打のたびに暗転が延びると
        // 「押しても進まない」感触になるため。
        f->pending = target;
        return;
    case VideoFadeState::kIn: {
        // 今の明るさ L = 256*t/dur から出発する kOut は「経過 dur - t の位置」。
        // 起点をその分だけ過去に置けば、この瞬間の輝度が連続につながる。
        uint32_t t = fade_elapsed(f, now);
        if (t > dur_ms) t = dur_ms;
        f->state    = VideoFadeState::kOut;
        f->start_ms = now - (dur_ms - t);
        f->pending  = target;
        return;
    }
    case VideoFadeState::kIdle:
    default:
        f->state    = VideoFadeState::kOut;
        f->start_ms = now;
        f->pending  = target;
        f->settled  = false;  // これから動く＝毎コマ描き直す
        return;
    }
}

uint32_t video_fade_level(VideoFade* f, uint32_t now, uint32_t dur_ms, int* out_swap_to) {
    if (out_swap_to) *out_swap_to = -1;
    if (f == nullptr) return 256;

    if (dur_ms == 0) {
        // フェード無し運用: kOut 要求が来ていたら行き先だけ即座に返して等倍へ。
        if (f->state == VideoFadeState::kOut && out_swap_to) *out_swap_to = f->pending;
        f->state   = VideoFadeState::kIdle;
        f->pending = -1;
        return 256;
    }

    switch (f->state) {
    case VideoFadeState::kOut: {
        const uint32_t t = fade_elapsed(f, now);
        if (t < dur_ms) return 256 - 256 * t / dur_ms;
        // 暗転の底: 行き先を1回だけ知らせ、フェードインへ。起点は「底に気づいた今」。
        // 呼び出し側はこの直後にサムネイルを読み直す（SD 読みの間は輝度 0 のまま＝見えない）。
        if (out_swap_to) *out_swap_to = f->pending;
        f->state    = VideoFadeState::kIn;
        f->start_ms = now;
        f->pending  = -1;
        return 0;
    }
    case VideoFadeState::kIn: {
        const uint32_t t = fade_elapsed(f, now);
        if (t < dur_ms) return 256 * t / dur_ms;
        // 完了。settled は落としたまま返し、呼び出し側にこのコマの等倍複写を1回させる
        //（video_fade_needs_redraw がその1回を消費して true→false にする）。
        f->state = VideoFadeState::kIdle;
        return 256;
    }
    case VideoFadeState::kIdle:
    default:
        return 256;
    }
}

bool video_fade_needs_redraw(VideoFade* f) {
    if (f == nullptr) return false;
    if (f->state != VideoFadeState::kIdle) return true;  // 動いている間は毎コマ
    if (f->settled) return false;
    f->settled = true;  // kIdle へ落ちた最初の1コマ。等倍の複写を1回だけさせる
    return true;
}

void video_fade_rebase(VideoFade* f, uint32_t now) {
    if (f == nullptr || f->state != VideoFadeState::kIn) return;
    f->start_ms = now;
}

uint16_t video_fade_scale565(uint16_t rgb565, uint32_t level) {
    // 等倍の早出し。level=256 は下の式でも厳密に恒等（31*256>>8=31）なので結果は変わらず、
    // 狙いは (a) 等倍コマで 17,424 画素ぶんの乗算を丸ごと省くこと、(b) 万一 level>256 が
    // 流れ込んだ時にフィールドが溢れて色が壊れるのを防ぐこと（reviewer 指摘）。
    if (level >= 256) return rgb565;
    const uint32_t r = (((rgb565 >> 11) & 0x1Fu) * level) >> 8;
    const uint32_t g = (((rgb565 >>  5) & 0x3Fu) * level) >> 8;
    const uint32_t b = ((  rgb565        & 0x1Fu) * level) >> 8;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

int video_fade_target(const VideoFade* f, int current) {
    if (f != nullptr && f->state == VideoFadeState::kOut) return f->pending;
    return current;
}
