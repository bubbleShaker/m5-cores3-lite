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

uint32_t video_bg_tone(uint32_t rgb, uint8_t target_max) {
    const uint32_t r = (rgb >> 16) & 0xFF;
    const uint32_t g = (rgb >> 8) & 0xFF;
    const uint32_t b = rgb & 0xFF;
    uint32_t mx = r;
    if (g > mx) mx = g;
    if (b > mx) mx = b;
    if (mx == 0) return 0;  // 真っ黒には色味が無い。0 除算も避ける

    // 等比スケール（四捨五入）。mx がそのまま target_max になり、他の成分は比率を保つ。
    const uint32_t t = target_max;
    uint32_t nr = (r * t + mx / 2) / mx;
    uint32_t ng = (g * t + mx / 2) / mx;
    uint32_t nb = (b * t + mx / 2) / mx;
    if (nr > 255) nr = 255;
    if (ng > 255) ng = 255;
    if (nb > 255) nb = 255;
    return (nr << 16) | (ng << 8) | nb;
}

uint32_t video_bg_lerp(uint32_t top, uint32_t bottom, int i, int n) {
    if (n <= 1) return top;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;

    // 成分ごとの重み付き平均 (a*(d-i) + b*i) / d。a,b,重みが全部非負なので切り捨ての向きが
    // 揺れず、i=0 で正確に a、i=d で正確に b になる（差の符号で丸めがずれる a+(b-a)*i/d 形を
    // 避けた）。最大値 255*239 + 119 は 32bit に余裕で収まる。
    const uint32_t d  = static_cast<uint32_t>(n - 1);
    const uint32_t wi = static_cast<uint32_t>(i);
    uint32_t out = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const uint32_t a = (top >> shift) & 0xFF;
        const uint32_t b = (bottom >> shift) & 0xFF;
        const uint32_t v = (a * (d - wi) + b * wi + d / 2) / d;
        out |= v << shift;
    }
    return out;
}
