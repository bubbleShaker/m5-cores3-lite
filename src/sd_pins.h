#pragma once
// CoreS3 の microSD(SPI) ピン定義（#157）。
//
// 通常ファーム(main.cpp)と転送専用ファーム(msc_main.cpp)の両方が使う。
// 別ファームとして独立させる設計だが、ピン番号は「ハードの事実」であって
// ロジックではないため共有する。片方だけ直して食い違うのを防ぐのが目的。
//
// ⚠ 注意: これらは M5GFX が LCD 用に使うピンと同一である。
//   M5GFX.cpp（CoreS3 初期化）は mosi=37 / miso=35 / sclk=36 を張り、
//   さらに GPIO35 を MISO と LCD の D/C で兼用している。
//   つまり SD アクセスと画面描画は同じ物理バスを奪い合う。別タスクから
//   同時に触らせてはいけない（main.cpp は両方とも loop タスクなので直列化されている）。

constexpr int kSdCsPin   = 4;
constexpr int kSdSckPin  = 36;
constexpr int kSdMisoPin = 35;
constexpr int kSdMosiPin = 37;
