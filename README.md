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

## 開発フロー

Issue 起票 → 実装 → PR → レビュー → マージ
