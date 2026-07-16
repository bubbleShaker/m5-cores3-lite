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
- [ ] **Step 2（次はここ）**: 端末で SD からフレーム + WAV を読んで再生。

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

## Step 2: 端末側 SD 再生（その次）
- microSD 初期化（M5Unified / SD.h）。CoreS3 は microSD スロット標準装備。
- `videoUpdate` で `video_frame_at(now - start, fps, frameCount)` → 該当フレームの
  JPEG を `M5.Display.drawJpg`（or drawJpgFile）で描画。
- 音声は `audio.wav` を分割ストリーミングして `playRaw`（TTS 経路の playWavBuffer が参考）。
  I2S は録音と共有なので排他に注意（既存 recordAndTranscribe の作法を踏襲）。
- フレーム番号と音声再生位置の同期（時刻基準は共通の millis 経過で揃える）。
- 短タップ = 一時停止/再開、長押し = メニュー復帰（既存の状態機械のまま）。

## 参考（既存コード）
- シーン状態機械・kScenes[]: src/main.cpp（「シーン表（巡回順）」付近）
- WAV 再生の共通末尾: src/main.cpp `playWavBuffer`（ヘッダ剥がし → playRaw）
- WAV ヘッダ解析（純粋・テスト済み）: src/wav.{h,cpp} `parse_wav_header`
- フレーム時刻の純粋ロジック: src/video.{h,cpp} `video_frame_at`
- 画像→RGB565 変換の既存ツール: tools/img2rgb565.py（作法の参考）

## 開発サイクル（毎回）
Issue 起票 → ブランチ → 実装（TDD・小さく刻む）→ reviewer サブエージェントで
レビュー（🔴must 解消）→ PR → マージ。GitHub: bubbleShaker/m5-cores3-lite。
