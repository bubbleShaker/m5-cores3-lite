#pragma once

#include <cstdint>

// 宝石図鑑のデータモデル（テーマ N / epic #27・P3）。
// 鉱物データは「事実」なので自前の小さなテーブルとして安全に持つ（著作物アセットは持たない）。
// 実機描画には依存しない純粋データ層なので、native で単体テストできる。
//
// 色は M5GFX と同じ RGB565（16bit）。描画は実機側（SceneDef）の責務で、ここでは「色の値」だけ持つ。
struct Gem {
    const char* name;      // 和名（例: ルビー）
    const char* locality;  // 代表的な産地
    const char* formula;   // 化学組成（化学式）
    const char* elements;  // 主要構成元素
    uint16_t    color;     // 宝石の代表色（RGB565）
};

// 図鑑に載っている宝石の数。
int gem_count();

// index 番目の宝石を返す。
//   index は [0, gem_count()) に正規化してから返すので、範囲外（負やオーバー）でも安全に1件返す。
//   図鑑が空（gem_count()==0）になることは無いが、その場合のみ nullptr を返す。
const Gem* gem_at(int index);
