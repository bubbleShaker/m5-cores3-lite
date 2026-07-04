#pragma once

// 話者(voiceroid)選択の純粋ロジック（Issue #105）。
// VOICEVOX の同梱話者から数体を厳選し、「選択インデックス」を左右で巡回させ、
// そのインデックスから中継 /tts に渡す speaker id と画面表示名を引くだけの純粋関数群。
// 実機・VOICEVOX・M5 表示に依存しないので native 環境でテストできる（volume.* と同じ流儀）。
//
// 既定は index 0 = ずんだもん（speaker 3・tts.ts の DEFAULT_SPEAKER と一致）。
// これにより voice_id を送っていなかった従来と同じ声が初期状態になる。

// 厳選した話者候補の数（>0 が定数で保証される）。
int voice_option_count();

// 選択インデックスに対応する VOICEVOX の speaker id を返す。
// 範囲外は巡回で正規化してから引く（負値・大きすぎる値でも安全）。
int voice_speaker_at(int index);

// 選択インデックスに対応する画面表示名（日本語）を返す。範囲外は同様に正規化する。
const char* voice_name_at(int index);

// タップX座標から「次の話者へ（右半分か）」を判定する純粋関数（volume と同じ二分）。
// 右半分(x >= screenW/2)=次へ、左半分=前へ。
bool voice_is_next_tap(int x, int screenW);

// インデックスを1つ進める/戻す（範囲 [0, count) を巡回）。純粋関数。
int voice_next(int index);
int voice_prev(int index);
