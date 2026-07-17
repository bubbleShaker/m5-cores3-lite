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

// フレーム番号（0基点）から SD 上のファイルパスを組み立てる純粋ロジック（Issue #150）。
// video_frame_at が返す 0..frame_count-1 の index を受け取り、1基点・5桁ゼロ埋めの
// "<dir>/frame_%05d.jpg" を buf に書く（例: dir="/video/sample", index=0 → "/video/sample/frame_00001.jpg"）。
// ゼロ埋め桁数・0/1基点のオフバイワンは実機でしか出ないバグになりやすいので純粋関数として native テストする。
//
// 成功時 true。buf/dir が null、または buf に収まらない（切り詰め）場合は false を返し
// buf の内容は使わせない（中途半端なパスで drawJpgFile を呼ばせないため）。
bool video_frame_path(char* buf, size_t buf_size, const char* dir, int index);
