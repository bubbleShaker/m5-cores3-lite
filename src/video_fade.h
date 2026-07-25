#pragma once
#include <stdint.h>

// 曲選択カルーセルのフェード遷移（Issue #209）の純粋ロジック。
// スワイプ → 現ディスクをフェードアウト → 暗転の底でサムネイル差し替え → フェードイン、
// という状態遷移と輝度の算出だけを扱う。描画・SD・時計に依存しない（millis 由来の now を
// 引数で受け取るだけ）ので native テストできる（scene/menu/video_frame_at と同じ作法）。
//
// 輝度は 0..256 の 257 段（256 = 等倍）。8bit の 255 ではなく 256 を等倍にするのは、
// 呼び出し側のスケール計算 (v * level) >> 8 が level=256 で厳密に恒等になるため。

enum class VideoFadeState : uint8_t { kIdle, kOut, kIn };

struct VideoFade {
    VideoFadeState state    = VideoFadeState::kIdle;
    uint32_t       start_ms = 0;   // 現フェーズの時刻起点
    int            pending  = -1;  // kOut 完了時に切り替える行き先（kOut 中のみ有効）
    // kIdle へ落ちた最初の1コマを示すラッチ（等倍に戻す複写を1回だけさせる）。
    bool           settled  = false;
};

// 初期化（kIdle・等倍）。シーン入場や選択画面の再表示で状態を確実に捨てるために使う。
void video_fade_init(VideoFade* f);

// スワイプによる行き先の指定。kIdle → kOut を開始。kOut 中 → 行き先だけ差し替え
// （連打で暗転をやり直さない＝引っかからない操作感）。kIn 中 → 今の明るさから連続に
// なるよう起点を逆算して kOut へ折り返す（輝度が跳ばないことは native テストで固定）。
void video_fade_request(VideoFade* f, uint32_t now, int target, uint32_t dur_ms);

// 毎コマの輝度（0..256）を求め、状態を1コマぶん進める（名前は level だが状態は変わる）。
// kOut が dur_ms に達した瞬間に *out_swap_to へ行き先を1回だけ書き（それ以外は -1）、kIn へ
// 遷移する。呼び出し側はそのタイミングでサムネイルを読み直す（暗転の底＝SD 読みの数十ms が
// 一番目立たない瞬間）。kIn が dur_ms に達したら kIdle（=256）へ戻る。
// dur_ms=0 はフェード無し（要求済みの行き先を即返して等倍のまま）。
uint32_t video_fade_level(VideoFade* f, uint32_t now, uint32_t dur_ms, int* out_swap_to);

// 「このコマで輝度を描き直す必要があるか」。フェード中に加えて、kIdle へ落ちた最初の1コマも
// true を返す（等倍に戻す複写を1回だけさせるため）。これが無いと、最後に適用した暗い絵が
// 次のスワイプまで残り続ける（表示用 Sprite は videoDiscPrepare 以外では書き換わらないため）。
// video_fade_level を呼んだ「後」に、その戻り値と合わせて使う。
bool video_fade_needs_redraw(VideoFade* f);

// kIn の起点を今に打ち直す（サムネイルの SD 読みが終わった時刻へ揃える）。
// 底のコマで起点を打つと、その直後の SD 読み（数十ms）ぶんフェードインが先に進んでしまい、
// 輝度 0 から一気に明るい値へ飛ぶ（片道が数コマしかないので 1 コマの損失が効く）。
// kIn 以外の状態では何もしない。
void video_fade_rebase(VideoFade* f, uint32_t now);

// 「今 UI が向かっている曲」。kOut 中は pending、それ以外は current を返す。
// 連打スワイプの基点と、フェード中の決定タップの解決（スワイプ直後のタップで
// 古い方が再生される違和感の回避）に使う。
int video_fade_target(const VideoFade* f, int current);
