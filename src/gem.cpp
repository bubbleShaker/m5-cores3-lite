#include "gem.h"

// 図鑑データ（自前・事実ベース）。組成式・代表的産地・主要構成元素は鉱物学の事実に基づく。
// 色は RGB565 で、各宝石の代表的な見た目に近づけた近似値（コメントに概略の #RRGGBB を添える）。
// 著作物（漫画タイトル・固有表現・公式アセット）は一切含めない。
static const Gem kGems[] = {
    // 名前        産地              組成式                     主要構成元素          色(RGB565)
    {"ルビー",       "ミャンマー",      "Al2O3 (Cr)",             "Al / O（発色:Cr）",  0xE08B},  // #E0115F 赤
    {"サファイア",   "スリランカ",      "Al2O3 (Fe,Ti)",          "Al / O（発色:Fe,Ti）", 0x0A97},  // #0F52BA 青
    {"エメラルド",   "コロンビア",      "Be3Al2Si6O18 (Cr)",      "Be / Al / Si / O",   0x564F},  // #50C878 緑
    {"ダイヤモンド", "南アフリカ",      "C",                      "C（炭素）",          0xDF1D},  // 透明〜白
    {"アメジスト",   "ブラジル",        "SiO2 (Fe)",              "Si / O（発色:Fe）",  0x9B39},  // #9966CC 紫
    {"トパーズ",     "ブラジル",        "Al2SiO4(F,OH)2",         "Al / Si / O / F",    0xFE4F},  // #FFC87C 黄
    {"ガーネット",   "インド",          "Fe3Al2(SiO4)3",          "Fe / Al / Si / O",   0x7882},  // #7B1113 赤褐
    {"オパール",     "オーストラリア",  "SiO2・nH2O",             "Si / O / H",         0xAE17},  // 乳白〜虹色
    {"ターコイズ",   "イラン",          "CuAl6(PO4)4(OH)8・4H2O", "Cu / Al / P / O / H", 0x471A},  // #40E0D0 青緑
    // ラピスラズリは単一鉱物ではなく岩石。式は主成分ラズライト(青金石)のもの。
    {"ラピスラズリ", "アフガニスタン",  "(Na,Ca)8(AlSiO4)6(S,SO4,Cl)2", "Na / Al / Si / O / S", 0x2313},  // #26619C 濃青
};

static const int kGemCount = static_cast<int>(sizeof(kGems) / sizeof(kGems[0]));

int gem_count() {
    return kGemCount;
}

const Gem* gem_at(int index) {
    if (kGemCount <= 0) return nullptr;  // 図鑑が空（通常起きない）。安全側に nullptr。

    // index を [0, kGemCount) に正規化する。負の index でも巡回的に正しい位置へ畳む。
    // （C++ の % は被除数の符号を引き継ぐため、負を足し戻してから再度 % で 0..count-1 に収める）
    int i = index % kGemCount;
    if (i < 0) i += kGemCount;
    return &kGems[i];
}
