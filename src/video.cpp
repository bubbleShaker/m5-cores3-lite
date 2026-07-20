#include "video.h"
#include <stdio.h>

int video_frame_at(uint32_t elapsed_ms, int fps, int frame_count) {
    if (fps <= 0 || frame_count <= 0) return 0;  // 再生できない時の安全値

    // 経過時間×fps÷1000 ＝ 先頭からの通算フレーム数。
    // uint64 で計算してから割る（elapsed_ms が長時間再生で大きくなっても *fps で
    // 32bit を溢れさせないため。先に uint64 へ広げてから掛ける）。
    uint64_t total = static_cast<uint64_t>(elapsed_ms) * static_cast<uint64_t>(fps) / 1000u;

    return static_cast<int>(total % static_cast<uint64_t>(frame_count));  // 末尾で先頭へループ
}

uint32_t video_cycle_at(uint32_t elapsed_ms, int fps, int frame_count) {
    if (fps <= 0 || frame_count <= 0) return 0;  // 再生できない時の安全値（video_frame_at と揃える）

    // video_frame_at と同じ total を作り、あちらが捨てる商の側を返す。
    // 計算式を揃えておかないと「番号は末尾なのに周回番号は次の周」といった境界の食い違いが出る。
    uint64_t total = static_cast<uint64_t>(elapsed_ms) * static_cast<uint64_t>(fps) / 1000u;

    return static_cast<uint32_t>(total / static_cast<uint64_t>(frame_count));
}

bool video_frame_path(char* buf, size_t buf_size, const char* dir, int index) {
    if (buf == nullptr || dir == nullptr || buf_size == 0) return false;
    if (index < 0) return false;  // 契約: 0基点の番号のみ。負値で frame_00000/frame_-0001 を作らせない

    // index は 0基点、ファイルは 1基点（frame_00001.jpg …）。
    // snprintf は「切り詰めなければ書けたはずの長さ」を返す。戻り値が buf_size 以上なら
    // 収まっていない＝切り詰められたので、中途半端なパスを使わせず false にする。
    int written = snprintf(buf, buf_size, "%s/frame_%05d.jpg", dir, index + 1);
    if (written < 0 || static_cast<size_t>(written) >= buf_size) return false;
    return true;
}
