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

// ---- #208: parse_wav_header_prefix（ストリーミング読みの先頭だけでヘッダを解析）----
// 動画音声のチャンクストリーミングでは、data の中身（数MB）を読む前にヘッダだけで
// フォーマットと PCM の位置を知る必要がある。既存 parse_wav_header は「data の中身が
// バッファ内に全部あること」を要求するため、prefix 版を対で検証する。

// data の中身が prefix に無くても、チャンクヘッダまで読めていれば解析できる
void test_prefix_parses_header_only() {
    auto w = make_wav(16000, 1, 16, 320);
    WavInfo info;
    // data チャンクの中身の直前＝44 バイトだけ読めている状態
    TEST_ASSERT_TRUE(parse_wav_header_prefix(w.data(), 44, w.size(), &info));
    TEST_ASSERT_EQUAL_UINT32(16000, info.sample_rate);
    TEST_ASSERT_EQUAL(44, info.data_offset);
    TEST_ASSERT_EQUAL(320, info.data_bytes);
}

// data がファイル全長に収まらない（途中で切れたファイル）は prefix 版でも弾く
void test_prefix_rejects_truncated_file() {
    auto w = make_wav(16000, 1, 16, 320);
    WavInfo info;
    TEST_ASSERT_FALSE(parse_wav_header_prefix(w.data(), 44, w.size() - 1, &info));
}

// prefix_len == file_len なら parse_wav_header と同じ結果（等価な特殊形）
void test_prefix_equals_full_parse() {
    auto w = make_wav(16000, 1, 16, 320);
    WavInfo a, b;
    TEST_ASSERT_TRUE(parse_wav_header(w.data(), w.size(), &a));
    TEST_ASSERT_TRUE(parse_wav_header_prefix(w.data(), w.size(), w.size(), &b));
    TEST_ASSERT_EQUAL_UINT32(a.sample_rate, b.sample_rate);
    TEST_ASSERT_EQUAL(a.data_offset, b.data_offset);
    TEST_ASSERT_EQUAL(a.data_bytes, b.data_bytes);
}

// prefix が fmt の途中で切れていたら解析しない（読めていないフィールドを読まない）
void test_prefix_rejects_cut_inside_fmt() {
    auto w = make_wav(16000, 1, 16, 320);
    WavInfo info;
    TEST_ASSERT_FALSE(parse_wav_header_prefix(w.data(), 20, w.size(), &info));
}

// prefix_len > file_len は契約違反として false（呼び出し側のバグを黙って通さない）
void test_prefix_rejects_prefix_longer_than_file() {
    auto w = make_wav(16000, 1, 16, 320);
    WavInfo info;
    TEST_ASSERT_FALSE(parse_wav_header_prefix(w.data(), w.size(), w.size() - 10, &info));
}

// prefix 内に収まらない LIST（file には収まる）は「そこで走査が止まる」だけ。
// data がその先にあれば見つからず false（prefix を伸ばして再挑戦する契約）。
void test_prefix_stops_at_chunk_beyond_prefix() {
    std::vector<uint8_t> chunks;
    put_fmt(chunks, 1, 1, 16000, 16);
    put_tag(chunks, "LIST");
    put_u32(chunks, 100);  // 中身 100B（prefix には入り切らない）
    for (int i = 0; i < 100; ++i) chunks.push_back(0);
    put_tag(chunks, "data");
    put_u32(chunks, 4);
    for (int i = 0; i < 4; ++i) chunks.push_back(0);
    auto w = wrap_riff(chunks);
    WavInfo info;
    // fmt と LIST ヘッダまでは読めているが、LIST の中身の途中で prefix が切れている
    TEST_ASSERT_FALSE(parse_wav_header_prefix(w.data(), 50, w.size(), &info));
    // prefix を伸ばして data のチャンクヘッダまで届けば解析できる
    TEST_ASSERT_TRUE(parse_wav_header_prefix(w.data(), w.size() - 4, w.size(), &info));
    TEST_ASSERT_EQUAL(4, info.data_bytes);
}

// sample_rate=0 は再生位置が進まない毒入力なので弾く（#208・SD 上の外部入力）
void test_rejects_zero_sample_rate() {
    auto w = make_wav(0, 1, 16, 16);
    WavInfo info;
    TEST_ASSERT_FALSE(parse_wav_header(w.data(), w.size(), &info));
}

// 3ch 以上はモノラル扱いで誤った速度で鳴るので弾く（#208）
void test_rejects_more_than_two_channels() {
    auto w = make_wav(16000, 3, 16, 24);
    WavInfo info;
    TEST_ASSERT_FALSE(parse_wav_header(w.data(), w.size(), &info));
    auto w2 = make_wav(16000, 2, 16, 24);  // 2ch は受理（境界の確認）
    TEST_ASSERT_TRUE(parse_wav_header(w2.data(), w2.size(), &info));
    TEST_ASSERT_EQUAL_UINT16(2, info.channels);
}

// ---- M3b-1（Issue #53）: write_wav（書く側）----

// wav_size は 44byte ヘッダ + PCM 本体。
void test_wav_size() {
    TEST_ASSERT_EQUAL_UINT32(44, wav_size(0));
    TEST_ASSERT_EQUAL_UINT32(44 + 8, wav_size(8));
}

// write→parse の往復一致。録音PCMをラップして自前パーサで矛盾なく読めること。
void test_write_then_parse_roundtrip() {
    const int16_t pcm[] = {0, 1000, -1000, 32767, -32768};
    const size_t  samples = sizeof(pcm) / sizeof(pcm[0]);
    std::vector<uint8_t> buf(wav_size(samples * 2));

    TEST_ASSERT_TRUE(write_wav(buf.data(), buf.size(), pcm, samples, 16000));

    WavInfo info;
    TEST_ASSERT_TRUE(parse_wav_header(buf.data(), buf.size(), &info));
    TEST_ASSERT_EQUAL_UINT32(16000, info.sample_rate);
    TEST_ASSERT_EQUAL_UINT16(1, info.channels);
    TEST_ASSERT_EQUAL_UINT16(16, info.bits_per_sample);
    TEST_ASSERT_EQUAL_UINT32(samples * 2, info.data_bytes);

    // data 本体が LE int16 でそのまま並んでいること。
    const uint8_t* d = buf.data() + info.data_offset;
    for (size_t i = 0; i < samples; ++i) {
        const uint16_t got = d[i * 2] | (d[i * 2 + 1] << 8);
        TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(pcm[i]), got);
    }
}

// samples=0 は「空 data の正当な WAV」として書け、parse も通る。
void test_write_empty_pcm() {
    std::vector<uint8_t> buf(wav_size(0));
    TEST_ASSERT_TRUE(write_wav(buf.data(), buf.size(), nullptr, 0, 16000));
    WavInfo info;
    TEST_ASSERT_TRUE(parse_wav_header(buf.data(), buf.size(), &info));
    TEST_ASSERT_EQUAL_UINT32(0, info.data_bytes);
}

// 容量不足・null では 1byte も書かず false（領域外を書かない）。
void test_write_rejects_small_cap_and_null() {
    const int16_t pcm[] = {1, 2, 3};
    std::vector<uint8_t> buf(wav_size(6));
    // cap が総バイトに 1 足りない → false。
    TEST_ASSERT_FALSE(write_wav(buf.data(), buf.size() - 1, pcm, 3, 16000));
    // out が null → false。
    TEST_ASSERT_FALSE(write_wav(nullptr, 100, pcm, 3, 16000));
    // pcm が null で samples>0 → false。
    TEST_ASSERT_FALSE(write_wav(buf.data(), buf.size(), nullptr, 3, 16000));
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
    RUN_TEST(test_prefix_parses_header_only);
    RUN_TEST(test_prefix_rejects_truncated_file);
    RUN_TEST(test_prefix_equals_full_parse);
    RUN_TEST(test_prefix_rejects_cut_inside_fmt);
    RUN_TEST(test_prefix_rejects_prefix_longer_than_file);
    RUN_TEST(test_prefix_stops_at_chunk_beyond_prefix);
    RUN_TEST(test_rejects_zero_sample_rate);
    RUN_TEST(test_rejects_more_than_two_channels);
    RUN_TEST(test_wav_size);
    RUN_TEST(test_write_then_parse_roundtrip);
    RUN_TEST(test_write_empty_pcm);
    RUN_TEST(test_write_rejects_small_cap_and_null);
    return UNITY_END();
}
