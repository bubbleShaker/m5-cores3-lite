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

// リトルエンディアンで詰める書き込み側（read_u16/u32 の対称）。
void write_u16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
}
void write_u32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xff);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xff);
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

namespace {
constexpr size_t   kWavHeaderSize = 44;  // 標準 canonical WAV ヘッダ長
constexpr uint16_t kChannelsMono  = 1;   // 録音はモノラル固定
constexpr uint16_t kBytesPerSample = 2;  // 16bit = 2byte
} // namespace

size_t wav_size(size_t pcm_bytes) {
    return kWavHeaderSize + pcm_bytes;
}

bool write_wav(uint8_t* out, size_t cap, const int16_t* pcm, size_t samples,
               uint32_t sample_rate) {
    if (out == nullptr) return false;
    // samples>0 のときだけ pcm 実体が要る（samples=0 は空 data の正当な WAV）。
    if (pcm == nullptr && samples != 0) return false;

    const size_t data_bytes = samples * kBytesPerSample;
    const size_t total      = wav_size(data_bytes);
    if (cap < total) return false;  // 容量不足なら 1byte も書かない（領域外防止）。

    // RIFF コンテナ。ChunkSize は "WAVE" 以降の総バイト＝36 + data_bytes。
    std::memcpy(out + 0, "RIFF", 4);
    write_u32(out + 4, static_cast<uint32_t>(36 + data_bytes));
    std::memcpy(out + 8, "WAVE", 4);

    // fmt チャンク（16byte の PCM 必須フィールド）。
    std::memcpy(out + 12, "fmt ", 4);
    write_u32(out + 16, 16);                 // Subchunk1Size
    write_u16(out + 20, kPcmFormat);         // AudioFormat = 1 (PCM)
    write_u16(out + 22, kChannelsMono);      // NumChannels = 1
    write_u32(out + 24, sample_rate);        // SampleRate
    const uint32_t block_align = kChannelsMono * kBytesPerSample;
    write_u32(out + 28, sample_rate * block_align);  // ByteRate
    write_u16(out + 32, static_cast<uint16_t>(block_align));  // BlockAlign
    write_u16(out + 34, kBits16);            // BitsPerSample = 16

    // data チャンク。中身は LE int16 をそのまま並べる。
    std::memcpy(out + 36, "data", 4);
    write_u32(out + 40, static_cast<uint32_t>(data_bytes));
    for (size_t i = 0; i < samples; ++i) {
        write_u16(out + kWavHeaderSize + i * kBytesPerSample,
                  static_cast<uint16_t>(pcm[i]));
    }
    return true;
}
