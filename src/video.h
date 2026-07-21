#pragma once
#include <stdint.h>
#include <stddef.h>

// 動画フレーム時刻の純粋ロジック（Issue #142）。
// 「再生開始からの経過ms」「フレームレート(fps)」「総フレーム数」だけから、
// 今表示すべきフレーム番号を決める。SD／ネットいずれの再生実装でも共有できる時間軸の核。
// scene(巡回)・menu(クランプ) と同じく、実描画・実データに依存しない純粋関数として native テストする。
//
// 末尾まで来たら先頭へ折り返す（ループ再生）。
//   fps <= 0 または frame_count <= 0 のときは 0 を返す（再生不能時の安全値）。
//   結果は必ず [0, frame_count) に収まる。
int video_frame_at(uint32_t elapsed_ms, int fps, int frame_count);

// 再生開始から「何周目か」を返す純粋ロジック（Issue #164）。0基点＝1周目は 0。
// video_frame_at が剰余で捨てている商そのもの。両者は同じ total から作られる表と裏の関係にある。
//
// 音声のループ再生（playRaw は一発再生でループ機能を持たない）を駆動するために使う。
// 「フレーム番号が戻ったか」で周回を検知すると、SD 読みが詰まって1回の更新間隔に一周ぶん以上
// 進んだ時に「番号が戻らないまま周を跨ぐ」ケースを取りこぼす。商を直接見れば原理的に起きない。
//   fps <= 0 または frame_count <= 0 のときは 0 を返す（video_frame_at と同じ安全値）。
uint32_t video_cycle_at(uint32_t elapsed_ms, int fps, int frame_count);

// フレーム番号（0基点）から SD 上のファイルパスを組み立てる純粋ロジック（Issue #150）。
// video_frame_at が返す 0..frame_count-1 の index を受け取り、1基点・5桁ゼロ埋めの
// "<dir>/frame_%05d.jpg" を buf に書く（例: dir="/video/sample", index=0 → "/video/sample/frame_00001.jpg"）。
// ゼロ埋め桁数・0/1基点のオフバイワンは実機でしか出ないバグになりやすいので純粋関数として native テストする。
//
// 成功時 true。buf/dir が null、または buf に収まらない（切り詰め）場合は false を返し
// buf の内容は使わせない（中途半端なパスで drawJpgFile を呼ばせないため）。
bool video_frame_path(char* buf, size_t buf_size, const char* dir, int index);

// パック方式（frames.bin）の索引から idx 番目の位置を取り出す純粋ロジック（Issue #170）。
//
// なぜパックするのか: FAT32 はディレクトリにインデックスを持たないため、1 ディレクトリに
// 2,355 個のフレームを置くとファイル名の解決が線形走査になり、終盤で 1 枚 1 秒かかった（#169）。
// 階層を分けても「ディレクトリを開くコスト」が新たに乗って逆に悪化する（実測・破棄済み）。
// そこで全フレームを 1 本にまとめ、番号→位置の辞書を自分たちの側に持つ。名前解決は
// 入場時の 1 回だけになり、以降は seek で直接飛べる。
//
// frames.bin のレイアウト:
//   [索引部] frame_count 個 × 8 バイト … offset(uint32 LE), length(uint32 LE)
//   [データ部] JPEG を連結（パディング無し）。offset はデータ部先頭からの相対値。
//
// 索引 1 レコードの幅（offset/length の uint32 が 2 本）。索引部の長さを求める側
// （端末の videoOpenPack）と読む側でこの値が食い違うと、データ部を索引として読むことになる。
// 定義を 1 つにするため公開する（変換ツール tools/video2frames.py 側とも対。片方だけ変えないこと）。
static const size_t kVideoPackEntrySize = 8;

// この方式で最も壊れやすいのは索引の読み違い（エンディアン・レコード幅・範囲外）なので、
// SD 上のファイルを外部入力として扱い、範囲検証まで含めてこの純粋関数に閉じ込める。
//   index      … 索引部だけを読み込んだバッファ
//   index_len  … その長さ（バイト）
//   frame_count… meta.txt の frames。索引部が本当にこの個数ぶんあるかの検証に使う
//   data_size  … データ部の実バイト数（ファイル全体 - 索引部）。offset+length の上限
// 範囲外・索引の長さ不足・データ部をはみ出す entry・length==0 では false を返し、
// 出力は書かない（呼び出し側に「壊れた索引でも読み進めてしまう」経路を作らせない）。
bool video_pack_entry(const uint8_t* index, size_t index_len, int frame_count,
                      int idx, uint32_t data_size,
                      uint32_t* out_offset, uint32_t* out_length);
