# PLAN: 動画再生シーン（SD フレーム再生）

このファイルは動画再生機能の段取りを永続化するもの。会話の要約で消えないよう、次セッションはここから再開する。

## ゴール
最初の選択画面（メニュー）の「動画再生」シーンで、指定した YouTube 動画
（https://youtu.be/Xbt0EqXOAjw）を CoreS3（ESP32-S3）で再生する。

## 大前提（ハード制約）
YouTube 生ストリーム（H.264/VP9・DRM）は ESP32-S3 単体では復号・デコード不可。
そのため **「動画 → JPEGフレーム列 + WAV」に PC 側で事前変換し、microSD から
`drawJpg` + `playRaw` で再生する** 方針を採用済み（MJPEG中継案は音声同期不可・
yt-dlp 依存で不安定なため不採用）。

## 進捗
- [x] **Step 0（完了・PR #143 / Issue #142）**: シーンの骨組み。
      メニュー登録 + 「準備中」表示 + フレーム時刻の純粋ロジック
      `video_frame_at(elapsed_ms, fps, frame_count)`（src/video.{h,cpp}）+ native テスト。
- [x] **Step 1（完了・Issue #144）**: 変換ツール `tools/video2frames.py` を追加。
      yt-dlp + ffmpeg で「JPEGフレーム列 + WAV + meta.txt(key=value manifest)」を出力。
      出力は `<outdir>/<name>/`（SD は `/video/<name>/`）。変換アセットは非コミット（.gitignore video/）。
- [~] **Step 2（進行中）**: 端末で SD からフレーム + WAV を読んで再生。
  - [x] **2a（完了・Issue #148）**: SD 初期化（SPI・CoreS3 固定ピン）+ `/video/sample/meta.txt` から
        fps/frames 取得 + `frame_00001.jpg` を1枚 `drawJpgFile` 表示。準備中表示を置換。
        純粋ロジック `meta_get_int`（src/meta.{h,cpp}・native テスト）追加。
        SD 作法は research/sd-video-playback.md にまとめた（重要: `SD.h` は `M5Unified.h` より
        **先に** include する。M5GFX が include 時に SD 対応を有効化するため）。
  - [x] **2b（完了・Issue #150）**: 連番フレーム送りループ（videoUpdate）。
        `video_frame_at` → `video_frame_path`（新・純粋関数：0基点index→1基点5桁ゼロ埋め frame_%05d.jpg、
        snprintf 切り詰め/負値ガード・native テスト）→ 変化時だけ drawJpgFile。同番号スキップ。
        欠け/破損フレームも「消化済み扱い」で g_videoLastIdx を更新し毎ループ SD を叩かない（reviewer 指摘）。
        起点 g_videoEnterMs は「1枚目を出し終えた今」に取り直す。実機での再生確認/fps 実測は別 Issue。
  - [~] **2c（進行中）**: 音声同期（audio.wav / playRaw）。
    - [x] **2c-1（完了・Issue #152）**: audio.wav 全体を PSRAM に載せ playRaw 一発で鳴らし、フレームと並走。
          `videoLoadAudio`（ps_malloc→parse_wav_header→playRaw、ベストエフォート＝無音でも絵は継続）。
          音声OFF(g_voiceEnabled)ガードで OFF=無音を維持。`videoExit` 新設で stop→free（use-after-free 回避、
          kScenes 動画行に登録）。起点 g_videoEnterMs は音声ロード直後に取り直し絵と音を揃える。
    - [ ] **2c-2（実測後・別Issue）**: 長尺で PSRAM に載らない/ズレが大きい場合のチャンクストリーミング。
          純粋関数 `wav_offset_at(elapsed_ms, rate, ch)`（millis経過→WAVバイトオフセット）を TDD で切り出す。
  - [ ] **2d**: タップ操作（一時停止/メニュー復帰）。

## Step 1: 変換ツール（tools/ に追加）— 次セッションのスコープ
まず Issue を起票してからブランチを切る（Issue 先行サイクル）。

### やること
- `tools/video2frames.sh`（または .py）を新規追加。yt-dlp + ffmpeg で:
  1. YouTube 動画をダウンロード（yt-dlp）
  2. ffmpeg で **低解像度・低fps の JPEG フレーム列** に変換
     - 画面は 320x240（setRotation(1)）。アスペクト維持でレターボックス、幅 320 目安
     - fps は 8〜12 目安（SD 読み + drawJpg のスループットに合わせて後で調整）
     - JPEG 品質は容量とのトレードオフ。連番 `frame_00001.jpg` …
  3. 音声を **WAV（16bit PCM・モノラル or ステレオ）** に抽出
     - サンプルレートは既存 TTS 経路に合わせるか、SD 再生に無理のない値で（要調整）
- 出力を microSD の決まったパス（例 `/video/<name>/frame_*.jpg` + `audio.wav`）に置く
  レイアウトを決め、README か tools 内 usage に明記
- 商標注意: 変換した動画アセットはリポジトリにコミットしない（.gitignore 済み方針を踏襲、
  ポケモン素材と同じ「実行時のみ・非コミット」の考え方）

### 設計メモ
- 変換パラメータ（fps/解像度/JPEG品質/音声レート）は端末の実測で詰める前提。
  まずは「動くフレーム列と WAV が出る」ところまで。数字は Step 2 で計測して確定。
- `video_frame_at` の fps は、この変換で決めた実 fps と一致させる（メタ情報として
  フレーム数・fps を manifest（例 `meta.json` か単純テキスト）で SD に置くと端末が読める）。

## Step 2: 端末側 SD 再生（次セッションのスコープ）
まず Issue を起票してからブランチを切る（Issue 先行サイクル）。実装は下の 2a→2d で小さく刻む。
GitHub: bubbleShaker/m5-cores3-lite。

### 前提（Step1 の成果物・すぐ使える）
- 変換ツールは `tools/video2frames.py`（マージ済み・PR #145 / Issue #144）。使い方は README「動画素材の変換」。
- SD の想定レイアウト: `/video/<name>/frame_%05d.jpg` + `audio.wav` + `meta.txt`。
- `meta.txt` は key=value テキスト。端末は `fps=` と `frames=` を読み、そのまま
  `video_frame_at(elapsed_ms, fps, frames)` に渡す（ArduinoJson 不要・1行ずつ split）。
- 実機で試すには、事前に PC で `python tools/video2frames.py <URL> --name sample` を実行し、
  出た `video/sample/` を microSD の `/video/sample/` にコピーしておく（アセットは非コミット）。

### 着手手順（小さく刻む）
- **2a: SD 初期化 + 1 フレーム表示**。まだ再生ループにしない。SD をマウントし、
  `/video/sample/meta.txt` を読んで fps/frames を取得、`frame_00001.jpg` を1枚
  `M5.Display.drawJpgFile(SD, path)` で出すところまで。videoEnter の「準備中」表示を置き換える。
  ※ CoreS3 は microSD スロット標準装備。SD 初期化 API はこのリポジトリにまだ無い（`grep SD src/main.cpp` は無ヒット）。
    M5Unified 併用時の SD.h/SD_MMC の作法を research/ で先に1本調べてからが安全。
- **2b: 連番フレーム送り**。`videoUpdate(now)` で `video_frame_at(now - g_videoEnterMs, fps, frames)`
  → その番号の `frame_%05d.jpg` を drawJpgFile。前フレームと同じ番号ならスキップ（ちらつき/SD負荷回避、
  既存 g_videoDots と同じ「変化時だけ描く」作法）。まず音声なしで絵が動くこと。
- **2c: 音声同期**。`audio.wav` を分割ストリーミングして `playRaw`。時刻基準はフレームと同じ
  millis 経過で揃える。WAV ヘッダ剥がしは `playWavBuffer`（src/main.cpp:401）と
  `parse_wav_header`（src/wav.*）を共有。I2S は録音と共有なので排他注意（recordAndTranscribe 作法）。
  ファイル全体を PSRAM に載せられない長さなら、チャンク読み→playRaw の継ぎ足しにする。
- **2d: 操作**。短タップ = 一時停止/再開（`videoOnTap` は現状 no-op、src/main.cpp:1996）、
  長押し = メニュー復帰（既存の状態機械のまま／変更不要）。

### 触るコードのアンカー（現在行・ズレる前提で grep 併用）
- 動画シーン本体: src/main.cpp `videoEnter`/`videoUpdate`/`videoOnTap`（1955〜1998 付近）。
  今は「準備中＋巡回ドット」の骨組み。ドット数計算 `video_frame_at(...)` をフレーム番号計算に置換する。
- シーン表: src/main.cpp `kScenes[]`（2002 付近）。「動画再生」行はそのまま（enter/update/onTap 差し替えのみ）。
- WAV 再生の共通末尾: src/main.cpp `playWavBuffer`（401）。
- WAV ヘッダ解析（純粋・テスト済み）: src/wav.{h,cpp} `parse_wav_header`。
- フレーム時刻の純粋ロジック: src/video.{h,cpp} `video_frame_at`（native テスト済み）。

### 純粋ロジックとして先に切り出せる候補（TDD しやすい）
- `meta.txt` の1行パース（"key=value" → 値取得）を純粋関数にして native テスト。
- 音声チャンクの「今の millis 経過に対応する WAV バイトオフセット」計算も純粋関数化できる。

## 参考（既存コード）
- シーン状態機械・kScenes[]: src/main.cpp（「シーン表（巡回順）」付近）
- WAV 再生の共通末尾: src/main.cpp `playWavBuffer`（ヘッダ剥がし → playRaw）
- WAV ヘッダ解析（純粋・テスト済み）: src/wav.{h,cpp} `parse_wav_header`
- フレーム時刻の純粋ロジック: src/video.{h,cpp} `video_frame_at`
- 画像→RGB565 変換の既存ツール: tools/img2rgb565.py（作法の参考）

## 開発サイクル（毎回）
Issue 起票 → ブランチ → 実装（TDD・小さく刻む）→ reviewer サブエージェントで
レビュー（🔴must 解消）→ PR → マージ。GitHub: bubbleShaker/m5-cores3-lite。
