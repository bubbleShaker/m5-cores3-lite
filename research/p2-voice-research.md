# P2 音声サブシステム 方式調査（声を出させる）

> 関連 Issue: #32 / 親 epic: #27（優先度 P2）
> 対象: M5Stack CoreS3-Lite (ESP32-S3 / スピーカー AW88298)
> 位置づけ: 実装前の調査。本メモでは実装しない。

## 0. 目的

羊やアバターに「声・音」を出させる音声サブシステムの実装に先立ち、CoreS3-Lite で取りうる方式を比較し、推奨構成・ライセンス注意・ロードマップを定める。
「ずんだもんに限らず任意の声」を **voice id 切替**で出せる構成を見据える。

## 1. ハードウェア制約と M5.Speaker でできること

- スピーカー: **AW88298 + 1W**（I2S 駆動）。M5Unified の `M5.Speaker` で制御する。
- `M5.Speaker` の主なメソッド:
  - `tone(freq, duration)` … 単音ブザー。効果音・メロディ向き（実装済みの `playBleat` がこれ）。
  - `playRaw(data, ...)` … 生 PCM を再生。サンプルレート指定（既定 44100Hz）、ステレオ/モノ等。
  - `playWav(wav_data, ...)` … WAV ヘッダ付きデータを再生。
- WAV の目安: **最大 500KB / 推奨 16000Hz・16bit**（公式 Speaker クラスの記載）。
- MP3/OGG は標準では非対応。公式 Advanced 例で **ESP8266Audio による MP3 デコード**や BLE ストリーミングの例がある（重い）。
- PSRAM 8MB / Flash 16MB / microSD があるので、音声バッファや WAV 保存に余裕はある。

> 含意: デバイス側は **PCM/WAV をそのまま再生**するのが素直。OGG/MP3 のデコードは避け、**変換はサーバ側**に寄せるのが現実的。

## 2. 方式の比較

| 方式 | 概要 | 語彙 | 実装難度 | ライセンス | 備考 |
|------|------|------|---------|-----------|------|
| ① トーン合成 | `tone()` で単音を組む | 効果音のみ | ★（実装済み） | 安全 | 「メェ」等。言葉は喋れない |
| ② オンデバイス WAV/PCM 再生 | 録音/生成済み WAV を Flash/SD から `playWav` | 固定フレーズ | ★★ | 音声素材に依存 | 即・確実に「音が出る」。語彙は固定 |
| ③ クラウド TTS | 中継サーバで音声生成→WAV/PCM をデバイスへ配信し再生 | 任意文章 | ★★★ | TTS規約に従う | 任意の声・voice id 切替。既存中継パターンに合致（本命） |

```mermaid
graph LR
    subgraph Device[CoreS3-Lite]
      SPK[M5.Speaker<br/>tone / playRaw / playWav]
    end
    subgraph Relay[中継サーバ Hono]
      TTS[/tts(text, voice_id)/]
    end
    ENG[VOICEVOX ENGINE 等]

    SPK -. ① tone .-> SPK
    WAVF[(埋め込み/SD WAV)] -- ② playWav --> SPK
    SPK -- ③ HTTP GET/POST --> TTS
    TTS --> ENG --> TTS
    TTS -- WAV/PCM --> SPK
```

## 3. 本命構成（③ クラウド TTS）の詳細

`device → relay /tts(text, voice_id) → 音声生成エンジン → WAV/PCM → device がストリーミング再生` という流れ。既存の `/chat`（Hono + Claude）と同じ中継パターンに **/tts エンドポイントを足す**だけで構成が揃う。

### 音声生成エンジンの候補

- **VOICEVOX ENGINE**（本命）: 自前ホストできる音声合成サーバ。`POST /audio_query`(text, speaker) → `POST /synthesis`(speaker) で **WAV を返す**。ずんだもん含む多数のキャラ＝**voice id 切替がそのまま speaker 指定で実現**できる。
- クラウド TTS（Google/Azure/Amazon Polly 等）: 安定・高品質だが声の選択肢や規約・課金が各社方針による。多言語が要るならここ。

### デバイス側の再生方針

- エンジンが返す WAV を **サーバ側で 16kHz/16bit へ整える**（デバイスの推奨フォーマットに合わせ、500KB 制限も考慮）。長文は文単位に分割。
- 受信した WAV を PSRAM にバッファして `playWav`、または PCM 化して `playRaw`。
- レイテンシ対策: 文ごとに細切れで取得→再生（擬似ストリーミング）。完全ストリーミング再生は実装が重いので MVP では一括取得で十分。

### voice id 切替（ずんだもん以外も）

- `/tts` のリクエストに `voice_id`（= VOICEVOX の speaker 番号など）を持たせ、サーバが対応エンジン/話者へ振り分ける。
- デバイス側は「文字列＋voice_id」を送るだけにして、声の実体はサーバの責務に分離する（クリーンな境界）。

## 4. ライセンス上の注意（public リポジトリ運用ゆえ重要）

- **VOICEVOX / ずんだもん**: 生成音声は **「VOICEVOX:ずんだもん」等のクレジット表記**を満たせば商用・非商用とも利用可。**話者ごとに規約が異なる**ので、使う話者の規約を都度確認する。音声を第三者に渡す場合は同じ規約遵守を求める必要がある。
- **生成音声アセットのコミット**: 規約条件（クレジット等）を満たせない形でリポジトリに音声を含めない。基本は **実行時生成・実行時取得**に寄せ、`README` にクレジットを明記する。
- **クラウド TTS**: 各社の利用規約・課金・帰属表示に従う。API キーは `secrets.h` 同様に **リポジトリ管理外**にする。

## 5. ロードマップ（MVP → 拡張）

| 段階 | 内容 | ねらい |
|------|------|--------|
| M0 | トーン合成（`playBleat`） | 実装済み。効果音で「音が出る」入口 |
| M1 | 埋め込み/SD の WAV を1個 `playWav` | 実機で「声/音声が出る」を最小確定（②） |
| M2 | 中継 `/tts` で固定文を VOICEVOX→WAV→再生 | サーバ経由の音声合成を通す（③の骨格） |
| M3 | `voice_id` 切替・任意文章・対話(K)連携 | 「ずんだもん以外も」「喋る」を実現 |

```mermaid
graph LR
    M0["M0 トーン合成<br/>(実装済)"] --> M1["M1 WAV 1個再生<br/>音が出る確定"]
    M1 --> M2["M2 /tts 固定文<br/>VOICEVOX→WAV"]
    M2 --> M3["M3 voice_id切替<br/>任意文章・対話連携"]
```

## 6. リスク / 未確認事項

- `playWav`/`playRaw` の実機挙動（フォーマット許容範囲・音量・ノイズ）は **M1 着手時に実機で確認**する。
- マイク(ES7210)とスピーカーの I2S 同時利用の可否・取り合い（対話で録音と再生を併用する場合）。
- VOICEVOX ENGINE のホスト先（ローカルPC/自宅サーバ/クラウド）と、デバイスからの到達性・レイテンシ。
- 長文時のメモリ（PSRAM バッファ）と分割再生の実装コスト。
- 500KB/WAV 制限に対する文分割の粒度。

## 7. 次の一歩（推奨）

実機が触れる状態になったら **M1（埋め込み or SD の WAV を1個 `playWav` で再生）** から着手する。これで「合成トーン」ではなく「本物の音声が出る」を最小実装で確定でき、その後 M2 でサーバ経由の VOICEVOX 合成へ広げる。

## 参考

- [M5Unified Speaker Class - m5-docs](https://docs.m5stack.com/en/arduino/m5unified/speaker_class)
- [M5Unified 例: Speaker_SD_wav_file](https://github.com/m5stack/M5Unified/blob/master/examples/Advanced/Speaker_SD_wav_file/Speaker_SD_wav_file.ino)
- [VOICEVOX ソフトウェア利用規約](https://voicevox.hiroshiba.jp/term/)
- [VOICEVOX ENGINE を使った音声合成 API の例](https://happy-shibusawake.com/voicevox_engine/1004/)
