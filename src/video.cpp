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

// レコードを固定長にしてあるのは、端末側のパースを「掛け算 1 回」に落として
// 毎フレームのコストをゼロに近づけるため。幅の定義は video.h の kVideoPackEntrySize。
bool video_pack_entry(const uint8_t* index, size_t index_len, int frame_count,
                      int idx, uint32_t data_size,
                      uint32_t* out_offset, uint32_t* out_length) {
    if (index == nullptr || out_offset == nullptr || out_length == nullptr) return false;
    if (frame_count <= 0) return false;
    if (idx < 0 || idx >= frame_count) return false;

    // 索引部が宣言された枚数ぶん本当にあるか。meta.txt の frames と frames.bin は別ファイルで、
    // 片方だけ古いまま SD に残ることが普通に起きる。ここで弾かないとデータ部を索引と誤読する。
    if (index_len / kVideoPackEntrySize < static_cast<size_t>(frame_count)) return false;

    const size_t base = static_cast<size_t>(idx) * kVideoPackEntrySize;

    // リトルエンディアンで組み立てる。memcpy で uint32 に読むとホストのバイト順に依存し、
    // 変換ツール（PC）と端末（ESP32-S3）が偶然一致しているだけの状態になる。明示的に組む。
    const uint32_t offset = static_cast<uint32_t>(index[base + 0])
                          | static_cast<uint32_t>(index[base + 1]) << 8
                          | static_cast<uint32_t>(index[base + 2]) << 16
                          | static_cast<uint32_t>(index[base + 3]) << 24;
    const uint32_t length = static_cast<uint32_t>(index[base + 4])
                          | static_cast<uint32_t>(index[base + 5]) << 8
                          | static_cast<uint32_t>(index[base + 6]) << 16
                          | static_cast<uint32_t>(index[base + 7]) << 24;

    if (length == 0) return false;  // 空フレームは描けない。0 バイト read を呼ばせない

    // offset + length がデータ部を超えないこと。uint64 で足すのは、壊れた索引の
    // offset=0xFFFFFFFF のような値で加算が 32bit を回り込み、範囲内に見えてしまうのを防ぐため
    // （これを見落とすと SD から読んだ長さで範囲外書き込みが起きる）。
    if (static_cast<uint64_t>(offset) + static_cast<uint64_t>(length)
        > static_cast<uint64_t>(data_size)) return false;

    *out_offset = offset;
    *out_length = length;
    return true;
}
