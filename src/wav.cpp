#include "wav.h"
#include <cstring>

namespace {

// リトルエンディアンで詰まれた値を読む。WAV は全フィールド LE 固定。
// 範囲チェックは呼び出し側で済ませてから使う（ここでは境界を見ない）。
uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// data から始まる 4 バイトが tag（"fmt " 等）と一致するか。
bool tag_eq(const uint8_t* p, const char* tag) {
    return std::memcmp(p, tag, 4) == 0;
}

constexpr uint16_t kPcmFormat   = 1;   // WAVE_FORMAT_PCM
constexpr uint16_t kBits16      = 16;  // 本実装が受理するビット深度

} // namespace

bool parse_wav_header(const uint8_t* data, size_t len, WavInfo* info) {
    if (data == nullptr || info == nullptr) return false;

    // RIFF ヘッダ（12byte）: "RIFF" + サイズ(4) + "WAVE"。最低でもこれが必要。
    if (len < 12) return false;
    if (!tag_eq(data, "RIFF")) return false;
    if (!tag_eq(data + 8, "WAVE")) return false;

    bool   have_fmt  = false;
    bool   have_data = false;
    WavInfo out;

    // 12 から先はチャンク列。各チャンク = ID(4) + サイズ(4) + 中身(size, 奇数なら1byteパディング)。
    size_t pos = 12;
    while (pos + 8 <= len) {  // 最低でも ID+サイズの 8byte が読めること
        const uint8_t* id   = data + pos;
        const uint32_t size = read_u32(data + pos + 4);
        const size_t   body = pos + 8;  // 中身の先頭

        // 中身が宣言サイズ分そろっていなければ壊れている（途中で切れた等）→ 失敗。
        if (size > len - body) return false;

        if (tag_eq(id, "fmt ")) {
            // fmt は最低 16byte（PCM の必須フィールド分）。
            if (size < 16) return false;
            const uint16_t fmt_code = read_u16(data + body + 0);
            out.channels        = read_u16(data + body + 2);
            out.sample_rate     = read_u32(data + body + 4);
            out.bits_per_sample = read_u16(data + body + 14);
            // 受理するのは 16bit リニア PCM のみ（playRaw が扱える形に限定）。
            if (fmt_code != kPcmFormat) return false;
            if (out.bits_per_sample != kBits16) return false;
            if (out.channels == 0) return false;
            have_fmt = true;
        } else if (tag_eq(id, "data")) {
            out.data_offset = body;
            out.data_bytes  = size;
            have_data = true;
        }
        // それ以外（LIST 等）は読み飛ばす。

        // 次チャンクへ。奇数サイズは 1byte パディングされる規約。
        size_t advance = size + (size & 1);
        pos = body + advance;
    }

    if (!have_fmt || !have_data) return false;
    // 16bit = 2byte 境界に PCM 長が乗っていること（半端なサンプルは弾く）。
    if (out.data_bytes % 2 != 0) return false;

    *info = out;
    return true;
}
