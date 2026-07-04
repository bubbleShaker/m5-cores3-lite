#pragma once
#include <cstdint>

// 発話 PCM から粗い「音量エンベロープ」を作る純粋ロジック（Issue #109）。
// 円形パーティクルの強さを実音声に「連動」させるため、utterance を buckets 個に等分し、
// 各区間の平均絶対振幅を求め、全区間のピークが 255 になるよう正規化して 0..255 で返す。
// 実機・M5・VOICEVOX に依存しないので native 環境でテストできる。
//
//   pcm     … 16bit PCM（モノラル、またはステレオのインターリーブでも近似として可）
//   samples … pcm の要素数（int16 の個数）
//   out     … 書き込み先（少なくとも buckets 要素）
//   buckets … 分割数（>0）。kEnvMaxBuckets を超える指定は内部で丸める。
//   返り値  … 実際に書いた bucket 数。無音・空入力なら全て 0 を書いて buckets を返す。

// out に書ける最大 bucket 数（内部一時バッファの上限）。
constexpr int kEnvMaxBuckets = 64;

int voice_envelope(const int16_t* pcm, int samples, uint8_t* out, int buckets);
