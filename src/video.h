#pragma once
#include <stdint.h>

// 動画フレーム時刻の純粋ロジック（Issue #142）。
// 「再生開始からの経過ms」「フレームレート(fps)」「総フレーム数」だけから、
// 今表示すべきフレーム番号を決める。SD／ネットいずれの再生実装でも共有できる時間軸の核。
// scene(巡回)・menu(クランプ) と同じく、実描画・実データに依存しない純粋関数として native テストする。
//
// 末尾まで来たら先頭へ折り返す（ループ再生）。
//   fps <= 0 または frame_count <= 0 のときは 0 を返す（再生不能時の安全値）。
//   結果は必ず [0, frame_count) に収まる。
int video_frame_at(uint32_t elapsed_ms, int fps, int frame_count);
