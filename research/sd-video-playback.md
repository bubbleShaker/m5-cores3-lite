# CoreS3 microSD からのフレーム再生（SD 作法・調査）

PLAN.md Step2a 着手前の調査。CoreS3 で microSD をマウントし、JPEG フレームと
manifest（meta.txt）を読むための最小作法をまとめる。実装の依存境界を固定するのが目的。

## 結論（そのまま使える最小作法）

CoreS3 の microSD は **SPI 接続**。ピンは固定で以下（M5Unified/M5GFX の新しめのバージョン前提）:

| 信号 | GPIO |
|------|------|
| CS   | 4    |
| SCK  | 36   |
| MISO | 35   |
| MOSI | 37   |

初期化コード（`M5.begin()` の後・SPI を張ってから SD をマウント）:

```cpp
#include <SPI.h>
#include <SD.h>
// SD ピン（CoreS3 固定）
static constexpr int kSdCsPin   = 4;
static constexpr int kSdSckPin  = 36;
static constexpr int kSdMisoPin = 35;
static constexpr int kSdMosiPin = 37;

SPI.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
if (!SD.begin(kSdCsPin, SPI, 25000000)) {
    // マウント失敗（未挿入・フォーマット不一致など）→ 画面にエラー表示
}
```

JPEG 表示は M5GFX（M5Unified 同梱）の `drawJpgFile` に `SD` を渡すだけ:

```cpp
M5.Display.drawJpgFile(SD, "/video/sample/frame_00001.jpg");
```

ファイル存在確認・テキスト読みは標準の Arduino `SD` API:

```cpp
if (SD.exists("/video/sample/meta.txt")) { ... }
auto f = SD.open("/video/sample/meta.txt");  // 既定は読み込み
while (f.available()) { /* f.read() で1バイトずつ */ }
f.close();
```

## バージョン要件（要確認）

- M5Stack Board Manager >= 3.2.5
- M5Unified >= 0.2.11
- M5GFX >= 0.2.18

このリポジトリは `platformio.ini` で `m5stack/M5Unified` を使用（バージョン固定なし＝最新解決）。
上記ピン定義は新しめの M5Unified を前提にした公式ドキュメント準拠なので、古い版だとピンが
異なる可能性がある。実機マウントで失敗したらまずここを疑う。

## SPI バス共有の注意

⚠ **訂正（#157）**: 当初ここに「画面(LCD)と microSD は物理的に別バス」と書いていたが、**誤り**。
M5GFX の CoreS3 初期化（`M5GFX.cpp`）は LCD に以下を割り当てており、SD と同一ピンである。

```cpp
bus_cfg.pin_mosi = GPIO_NUM_37;   // SD の MOSI と同じ
bus_cfg.pin_miso = GPIO_NUM_35;   // SD の MISO と同じ
bus_cfg.pin_sclk = GPIO_NUM_36;   // SD の SCK と同じ
bus_cfg.pin_dc   = GPIO_NUM_35;   // MISOとLCD D/CをGPIO35でシェアしている
```

つまり **SD アクセスと画面描画は同じ物理バスを奪い合う**。とくに GPIO35 は SD の MISO と
LCD の D/C の兼用なので、SD が出力している最中に LCD の D/C を駆動すると衝突する。

`main.cpp` が問題なく動いているのは、SD アクセスも描画もすべて `loop` タスク内で直列に
実行されているからであって、バスが分かれているからではない。**別タスク（USB タスク等）から
SD と画面を同時に触らせてはいけない**。実際 #157 の転送専用ファームでは、MSC コールバックが
USB タスクで走るため、MSC 開始後は `loop` から一切描画しない設計にしている。

なお録音/再生の I2S は別系統なので、この制約とは無関係。

## この調査から Step2a に確定したこと

- SD 初期化は `M5.begin()` の後、`videoEnter`（動画シーン入場時）で行う。
  常時マウントしない＝他シーンに影響を与えない。失敗時は画面にメッセージ表示。
- meta.txt は `SD.open` で丸ごと `String`/バッファに読み、純粋関数 `meta_get_int` に渡す。
- 1 枚目は `M5.Display.drawJpgFile(SD, "/video/sample/frame_00001.jpg")`。

## Sources
- [CoreS3 microSD Card — m5-docs](https://docs.m5stack.com/en/arduino/m5cores3/sdcard)
- [CoreS3 Image Display — m5-docs](https://docs.m5stack.com/en/arduino/m5cores3/pic)
