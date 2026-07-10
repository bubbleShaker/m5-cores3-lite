# m5-cores3-lite

M5Stack CoreS3-Lite (ESP32-S3) で遊び・学びながら開発するためのリポジトリ。

## ハードウェア概要

- **SoC**: ESP32-S3 (Xtensa LX7 dual-core 240MHz / Flash 16MB / PSRAM 8MB)
- **ディスプレイ**: 2.0" 静電容量タッチ IPS (320x240)
- **カメラ**: GC0308 (0.3MP)
- **センサ**: 6軸IMU (BMI270) / 3軸地磁気 (BMM150) / 近接・照度 (LTR-553ALS)
- **オーディオ**: デュアルマイク (ES7210) / 1W スピーカー (AW88298)
- **その他**: microSD / Wi-Fi 2.4GHz / RTC (BM8563) / Grove PORT.A

## 開発環境

- [PlatformIO](https://platformio.org/) (Arduino framework / C++)

## ディレクトリ構成

| ディレクトリ | 役割 |
|------------|------|
| `research/` | 調査メモ（実装できること・技術調査） |
| `summary/`  | 実装完了後の概要 |
| `knowledge/`| ハードウェアで分からなかったことの解説 |
| `prom/`     | 指示プロンプト置き場 |

## 主要ライブラリ（OSS）

| ライブラリ | 用途 |
|----------|------|
| [M5Unified](https://github.com/m5stack/M5Unified) | M5Stack 統合ドライバ（画面・IMU・音声など） |
| [ArduinoJson](https://arduinojson.org/) | JSON パース（中継サーバ通信） |
| [M5Stack-Avatar](https://github.com/meganetaaan/m5stack-avatar) | 通称「スタックチャン」。定番の顔描画ライブラリ（表情・口パク・まばたき） |

## セットアップ

### Wi-Fi 認証情報（必須・初回のみ）

Wi-Fi 認証情報はリポジトリに含めない。テンプレートをコピーして自分の値を入れる。

```sh
cp src/secrets.h.example src/secrets.h
# src/secrets.h を編集して WIFI_SSID / WIFI_PASS を自分の Wi-Fi に書き換える
```

`src/secrets.h` は `.gitignore` 済みなのでコミットされない。

### ビルド・書き込み・テスト

```sh
pio run -e m5stack-cores3            # 実機ビルド
pio run -e m5stack-cores3 -t upload  # 実機へ書き込み
pio test -e native                   # ホストPCで純粋ロジックの単体テスト
```

## 開発フロー

Issue 起票 → 実装 → PR → レビュー → マージ

## 商標・ライセンス表記

ポケモン図鑑シーン（P4）は、情報・スプライト画像・鳴き声を中継サーバ経由で**実行時にのみ取得**し、当リポジトリには一切コミットしない。

> Pokémon and Pokémon character names are trademarks of Nintendo / Creatures Inc. / GAME FREAK inc.
