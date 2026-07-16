#include "video.h"

int video_frame_at(uint32_t elapsed_ms, int fps, int frame_count) {
    if (fps <= 0 || frame_count <= 0) return 0;  // 再生できない時の安全値

    // 経過時間×fps÷1000 ＝ 先頭からの通算フレーム数。
    // uint64 で計算してから割る（elapsed_ms が長時間再生で大きくなっても *fps で
    // 32bit を溢れさせないため。先に uint64 へ広げてから掛ける）。
    uint64_t total = static_cast<uint64_t>(elapsed_ms) * static_cast<uint64_t>(fps) / 1000u;

    return static_cast<int>(total % static_cast<uint64_t>(frame_count));  // 末尾で先頭へループ
}
