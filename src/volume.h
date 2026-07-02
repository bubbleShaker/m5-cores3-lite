#pragma once
#include <cstdint>

// 音量調整の純粋ロジック（Issue #70）。実機の M5.Speaker に依存せず、
// 「音量レベル」を増減・クランプし、実機の setVolume 値へ変換するだけの純粋関数群。
// これにより native 環境（実機なし）でも境界値（上限・下限）をテストできる。
//
// 音量は 0〜kVolumeMax の整数レベルで扱う（画面の音量バーの目盛りと一致させる）。
// レベル0=無音、kVolumeMax=最大。実機へは volume_to_speaker で 0〜255 に直して渡す。

constexpr int kVolumeMax     = 7;  // 最大レベル（0〜7 の8段階）
constexpr int kVolumeDefault = 5;  // 初期レベル（従来のベタ書き 180 に最も近い 182 相当）

// レベルを1段上げる/下げる（範囲 [0, kVolumeMax] にクランプ）。純粋関数。
int volume_up(int level);
int volume_down(int level);

// 音量レベル(0〜kVolumeMax)を実機 setVolume 値(0〜255)へ変換する。
// level*255/kVolumeMax の線形マッピング。範囲外レベルは内部でクランプする。
uint8_t volume_to_speaker(int level);

// タップX座標から「音量アップか（右半分か）」を判定する純粋関数。
// 画面右半分(x >= screenW/2)のタップ=アップ、左半分=ダウン。
bool volume_is_up_tap(int x, int screenW);
