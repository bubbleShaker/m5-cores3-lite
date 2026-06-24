#pragma once
#include <cstddef>
#include <cstdint>

// P2 音声 MVP / M2b（Issue #48）。
// 中継サーバ /tts が返す WAV バイト列から「PCM 本体の位置とフォーマット」を取り出す純粋ロジック。
// M5.Speaker.playRaw は「ナマの int16 PCM 列」を要求するため、WAV のヘッダを剥がす必要がある。
//
// ※ WAV は RIFF コンテナ＝「チャンク（4byte ID + 4byte サイズ + 中身）」の連なり。
//    先頭固定44byte とは限らず（LIST 等が挟まることがある）、fmt / data を走査して探すのが堅牢。
// millis() 等に依存せず決定論的なので、実機なしで native テストできる。

// 解析結果。data_offset から data_bytes 分が PCM 本体（リトルエンディアン int16）。
struct WavInfo {
    uint32_t sample_rate    = 0;  // 例: 16000
    uint16_t channels       = 0;  // 例: 1（モノラル）
    uint16_t bits_per_sample = 0; // 本実装が受理するのは 16 のみ
    size_t   data_offset    = 0;  // PCM 本体の先頭バイト位置（data チャンク中身の先頭）
    size_t   data_bytes     = 0;  // PCM 本体のバイト数
};

// WAV バイト列を解析して info を埋める純粋関数。
//   data … WAV 先頭ポインタ / len … バイト数
//   返り値 true … RIFF/WAVE で 16bit PCM の fmt と data を矛盾なく見つけた
//   返り値 false … マジック不一致・16bit PCM 以外・チャンクが境界外、等（info は未定義扱い）
// 壊れた入力でも領域外アクセスしないこと（バッファは信用しない）を最優先に実装する。
bool parse_wav_header(const uint8_t* data, size_t len, WavInfo* info);

// ---- P2 音声 M3b-1（Issue #53）: WAV を「書く」純粋ロジック ----
// 録音した 16bit モノラル PCM を中継 /stt に送るため、標準44byteヘッダで WAV にラップする。
// 上の parse_wav_header（読む）の対称で、write→parse が往復一致するのが設計のゴール。

// 出力 WAV の総バイト数。標準44byteヘッダ + PCM 本体。
// pcm_bytes はサンプル数 * 2（16bit）。呼び出し側がバッファ確保の見積りに使う。
size_t wav_size(size_t pcm_bytes);

// 16bit モノラル PCM を WAV バイト列として out に書く純粋関数。
//   out / cap     … 書き込み先と容量（cap 不足なら何も書かず false＝領域外を書かない）
//   pcm / samples … int16 PCM 配列とサンプル数（samples=0 は空 data の正当な WAV）
//   sample_rate   … 例: 16000
//   返り値 true … wav_size(samples*2) バイトを out に書けた
// parse_wav_header と往復一致する標準フォーマットのみ出力する。
bool write_wav(uint8_t* out, size_t cap, const int16_t* pcm, size_t samples,
               uint32_t sample_rate);
