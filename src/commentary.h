#pragma once
#include <string>

#include "gem.h"      // Gem
#include "pokemon.h"  // Pokemon

// P5「掛け合わせ」/ M1（epic #27 / Issue #92）。
// カードのデータ（宝石 / ポケモン）から「キャラが声で読み上げる解説文」を組み立てる純粋関数。
//
// P5 の統合に必要な実機側の部品（speakTts(text) で中継 /tts に投げて鳴らす経路、
// カードシーン、口パク見積り）は既に揃っている。足りないのは「データ → 喋る文字列」だけなので、
// ここを greeting.cpp / voice.cpp と同じ「ハード非依存・決定論的・native テスト可」な層として先に確立する。
// 実機シーンはこの戻り値を speakTts() に渡すだけで済む（配線は別 Issue：M2）。
//
// 語り口は中立の丁寧体（〜です）。話者選択で別 voiceroid も選べるため、キャラに依存しない
// 語尾にしている（声質は voice id 側の責務であり、読み上げテキストは声と切り離す）。
//
// 設計方針（既存の純粋層と同じ「安全側フォールバック」）:
//   - フィールドが欠落（nullptr / 空文字）していても落とさず、その項目だけ省いて文を組み立てる。
//   - 名前が無いときだけ既定の呼称（「この宝石」/「このポケモン」）へフォールバックする。
//   - millis() 等に依存せず、同じ入力 → 必ず同じ文字列（native で決定論的にテストできる）。

// 宝石カードの解説文を組み立てる。
//   例: gem_commentary({"ルビー","ミャンマー","Al2O3 (Cr)","Al / O（発色:Cr）",…})
//       -> "ルビーです。産地はミャンマー、組成は Al2O3 (Cr)、主な元素は Al / O（発色:Cr）です。"
std::string gem_commentary(const Gem& g);

// ポケモンカードの解説文を組み立てる。
//   例: pokemon_commentary({1,"フシギダネ",…,"たねポケモン","grass, poison","〈説明〉"})
//       -> "フシギダネ、たねポケモンです。タイプは grass, poison。〈説明〉"
std::string pokemon_commentary(const Pokemon& p);
