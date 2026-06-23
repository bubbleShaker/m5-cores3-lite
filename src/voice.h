#pragma once
#include <cstdint>

// P2 音声 MVP / M1（Issue #44）。
// 「本物の波形データ(PCM)」を生成して M5.Speaker.playRaw で鳴らす経路を通す純粋ロジック。
// tone() の単音と違い、ここでは波形そのものを数値列(PCM)として組み立てる
// （クラウド TTS でもサーバが返す PCM を playRaw で鳴らす＝同じ経路をここで先に確立する）。
//
// ※ PCM＝音の波形を「1秒間に sample_rate 個の 16bit 整数」で表したナマのデータ。

constexpr int kVoiceSampleRate = 16000;  // 16kHz（M5.Speaker 推奨フォーマット）
constexpr int kBaaSamples      = 6400;   // 0.4 秒（16000 * 0.4）。バッファはこの大きさを確保する。

// 合成した「メェ」を 16bit モノラル PCM として out[] に書き込む純粋関数。
//   out      … 書き込み先（int16 の配列）
//   capacity … out が受け取れる最大サンプル数（これを超えて書かない）
//   返り値   … 実際に書き込んだサンプル数（min(kBaaSamples, capacity)）
// millis() 等に依存せず決定論的（同じ引数 → 必ず同じ波形）。実機なしで native テストできる。
int voice_baa_pcm(int16_t* out, int capacity);
