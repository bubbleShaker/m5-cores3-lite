#include <unity.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include "wav.h"

// 音声 M2b（Issue #48）の WAV ヘッダ解析を検証する。
// 「壊れたバッファを信用しない」純粋ロジックなので、正常系だけでなく
// 不正フォーマット・途中切れ・余分チャンク跨ぎを native で固める。

void setUp(void) {}
void tearDown(void) {}

namespace {

// テスト用に「最小限の WAV」を組み立てるヘルパ。
// fmt(16byte) + 任意の追加チャンク群 + data(pcm) を素直に並べる。
void put_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(x & 0xFF);
    v.push_back((x >> 8) & 0xFF);
}
void put_u32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x & 0xFF);
    v.push_back((x >> 8) & 0xFF);
    v.push_back((x >> 16) & 0xFF);
    v.push_back((x >> 24) & 0xFF);
}
void put_tag(std::vector<uint8_t>& v, const char* t) {
    v.insert(v.end(), t, t + 4);
}

// fmt チャンク（16byte PCM コア）を書く。format/channels/bits を差し替えてエラー系も作れる。
void put_fmt(std::vector<uint8_t>& v, uint16_t fmt, uint16_t ch,
             uint32_t rate, uint16_t bits) {
    put_tag(v, "fmt ");
    put_u32(v, 16);
    put_u16(v, fmt);
    put_u16(v, ch);
    put_u32(v, rate);
    put_u32(v, rate * ch * (bits / 8));  // byteRate
    put_u16(v, ch * (bits / 8));         // blockAlign
    put_u16(v, bits);
}

// RIFF/WAVE ヘッダ＋中身 chunks を包んで完成形にする。
std::vector<uint8_t> wrap_riff(const std::vector<uint8_t>& chunks) {
    std::vector<uint8_t> v;
    put_tag(v, "RIFF");
    put_u32(v, static_cast<uint32_t>(4 + chunks.size()));  // "WAVE" + chunks
    put_tag(v, "WAVE");
    v.insert(v.end(), chunks.begin(), chunks.end());
    return v;
}

// 標準的な 16kHz/モノラル/16bit の WAV（PCM = pcm_bytes バイト）。
std::vector<uint8_t> make_wav(uint32_t rate, uint16_t ch, uint16_t bits,
                              size_t pcm_bytes) {
    std::vector<uint8_t> chunks;
    put_fmt(chunks, 1, ch, rate, bits);
    put_tag(chunks, "data");
    put_u32(chunks, static_cast<uint32_t>(pcm_bytes));
    for (size_t i = 0; i < pcm_bytes; ++i) chunks.push_back(static_cast<uint8_t>(i & 0xFF));
    return wrap_riff(chunks);
}

} // namespace

// 正常な 16kHz/モノラル/16bit WAV をパースできる
void test_parses_standard_wav() {
    auto w = make_wav(16000, 1, 16, 320);
    WavInfo info;
    TEST_ASSERT_TRUE(parse_wav_header(w.data(), w.size(), &info));
    TEST_ASSERT_EQUAL_UINT32(16000, info.sample_rate);
    TEST_ASSERT_EQUAL_UINT16(1, info.channels);
    TEST_ASSERT_EQUAL_UINT16(16, info.bits_per_sample);
    TEST_ASSERT_EQUAL_UINT32(320, info.data_bytes);
    // data_offset から data_bytes 分が実バッファ内に収まっていること
    TEST_ASSERT_TRUE(info.data_offset + info.data_bytes <= w.size());
    // 先頭 PCM バイトが期待通り（中身まで指せている）
    TEST_ASSERT_EQUAL_UINT8(0, w[info.data_offset]);
}

// fmt と data の間に LIST 等の余分チャンクが挟まっても data を見つけられる
void test_skips_extra_chunks() {
    std::vector<uint8_t> chunks;
    put_fmt(chunks, 1, 1, 16000, 16);
    // 余分な LIST チャンク（中身 5byte → 奇数なのでパディング1byte入る）
    put_tag(chunks, "LIST");
    put_u32(chunks, 5);
    for (int i = 0; i < 5; ++i) chunks.push_back(0xAA);
    chunks.push_back(0x00);  // パディング
    put_tag(chunks, "data");
    put_u32(chunks, 4);
    for (int i = 0; i < 4; ++i) chunks.push_back(0x11);
    auto w = wrap_riff(chunks);

    WavInfo info;
    TEST_ASSERT_TRUE(parse_wav_header(w.data(), w.size(), &info));
    TEST_ASSERT_EQUAL_UINT32(4, info.data_bytes);
    TEST_ASSERT_EQUAL_UINT8(0x11, w[info.data_offset]);
}

// RIFF/WAVE マジックが違えば reject
void test_rejects_bad_magic() {
    auto w = make_wav(16000, 1, 16, 16);
    w[0] = 'X';  // "RIFF" を壊す
    WavInfo info;
    TEST_ASSERT_FALSE(parse_wav_header(w.data(), w.size(), &info));
}

// 16bit 以外（24bit 等）は reject（playRaw が扱えない）
void test_rejects_non_16bit() {
    auto w = make_wav(16000, 1, 24, 24);
    WavInfo info;
    TEST_ASSERT_FALSE(parse_wav_header(w.data(), w.size(), &info));
}

// PCM 以外のフォーマットコード（例: float=3）は reject
void test_rejects_non_pcm() {
    std::vector<uint8_t> chunks;
    put_fmt(chunks, 3, 1, 16000, 16);  // format=3（IEEE float）
    put_tag(chunks, "data");
    put_u32(chunks, 4);
    for (int i = 0; i < 4; ++i) chunks.push_back(0);
    auto w = wrap_riff(chunks);
    WavInfo info;
    TEST_ASSERT_FALSE(parse_wav_header(w.data(), w.size(), &info));
}

// data チャンクが宣言サイズ分そろっていない（途中で切れた）なら reject
void test_rejects_truncated_data() {
    auto w = make_wav(16000, 1, 16, 320);
    w.resize(w.size() - 100);  // 末尾を削る＝data が宣言より短い
    WavInfo info;
    TEST_ASSERT_FALSE(parse_wav_header(w.data(), w.size(), &info));
}

// 12byte 未満・nullptr など極端な入力でも落ちず false
void test_rejects_too_short_and_null() {
    uint8_t tiny[4] = {'R', 'I', 'F', 'F'};
    WavInfo info;
    TEST_ASSERT_FALSE(parse_wav_header(tiny, sizeof(tiny), &info));
    TEST_ASSERT_FALSE(parse_wav_header(nullptr, 100, &info));
    auto w = make_wav(16000, 1, 16, 16);
    TEST_ASSERT_FALSE(parse_wav_header(w.data(), w.size(), nullptr));
}

// data はあるが fmt が無い → reject
void test_rejects_missing_fmt() {
    std::vector<uint8_t> chunks;
    put_tag(chunks, "data");
    put_u32(chunks, 4);
    for (int i = 0; i < 4; ++i) chunks.push_back(0);
    auto w = wrap_riff(chunks);
    WavInfo info;
    TEST_ASSERT_FALSE(parse_wav_header(w.data(), w.size(), &info));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_standard_wav);
    RUN_TEST(test_skips_extra_chunks);
    RUN_TEST(test_rejects_bad_magic);
    RUN_TEST(test_rejects_non_16bit);
    RUN_TEST(test_rejects_non_pcm);
    RUN_TEST(test_rejects_truncated_data);
    RUN_TEST(test_rejects_too_short_and_null);
    RUN_TEST(test_rejects_missing_fmt);
    return UNITY_END();
}
