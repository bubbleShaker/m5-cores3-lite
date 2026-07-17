# 動画変換ツール video2frames.py（動画再生シーン Step1 完了）

- Issue: #144 / PR: #145（squash マージ済み）
- 位置づけ: PLAN.md の「動画再生シーン」Step1。全体は Step0(骨組み・済) → **Step1(変換ツール・本件)** → Step2(端末側 SD 再生・次)。

## 何を作ったか
`tools/video2frames.py` を追加した。YouTube 等の動画を **PC 側で**
「JPEGフレーム列 + WAV + meta.txt」へ事前変換する CLI ツール。

```
python tools/video2frames.py <URL または ローカル動画> --name sample
# 既定で ./video/sample/ に出力（--outdir で SD マウント先を直指定も可）
```

出力レイアウト（この `<name>/` ごと microSD の `/video/` 下へ置く）:

| ファイル | 内容 |
|---------|------|
| `frame_%05d.jpg` | 320x240 アスペクト維持レターボックスの連番 JPEG |
| `audio.wav` | 16bit PCM（既定 mono / 16kHz） |
| `meta.txt` | manifest `fps=` / `frames=` / `width=` / `height=` / `sample_rate=` / `channels=` |

## なぜこの設計か（判断の記録）
- **なぜ PC 側で事前変換か**: ESP32-S3 単体では YouTube 生ストリーム（H.264/VP9・DRM）を
  復号・デコードできない（ハード制約）。重い復号を PC で済ませ、端末は `drawJpg` + `playRaw` で
  「貼る・鳴らす」だけにする。MJPEG 中継案は音声同期不可・yt-dlp 依存で不安定なため不採用。
- **なぜ manifest が JSON でなく key=value テキストか**: 端末側(Step2)で ArduinoJson 依存を
  増やさず 1 行ずつ split で読めるようにするため。`fps`/`frames` は端末の
  `video_frame_at(elapsed_ms, fps, frames)` にそのまま渡す時間軸の基準になる。
- **なぜ Python か**: 既存 `tools/img2rgb565.py` と言語・作法を揃えるため。内部は yt-dlp + ffmpeg の
  サブプロセス起動ラッパ。コマンド組み立て（`build_frame_cmd` / `build_audio_cmd` /
  `build_scale_filter` / `render_meta`）を副作用のある起動から純粋関数として分離し、
  img2rgb565.py の「純粋関数 + 薄い main」作法を踏襲＝テスト・レビューしやすい。

## レビュー反映（reviewer サブエージェント）
- 🔴 `--name` のパストラバーサル検証を追加（`safe_subdir_name`。`..`/絶対パス/区切り文字を拒否）。
  `os.path.join('video', name)` が `video/` の外へ抜けるのを防ぐ。
- 🔴 変換前に古い `frame_*.jpg` をクリア。ffmpeg `-y` は個別上書きするだけなので、短い動画へ
  再変換すると前回の長い連番が残り、`count_frames` が `frames` を過大計上 → 端末が存在しない
  フレームを指す不具合になる。それを予防。
- 🟡 yt-dlp を `bestvideo+bestaudio/best` に（progressive `best` 単体の 360p 上限/不在対策。
  分離ストリームを ffmpeg でマージ）。
- 🟡 ffmpeg/yt-dlp 失敗時を `run_step` で日本語エラー停止（`require_tool` と体験統一）。

## 動作確認
実機なしで検証: ffmpeg 合成動画（2秒）をローカル入力経路で変換し、20フレーム(10fps)・
`audio.wav`・`meta.txt` が正しく生成されることを確認。`safe_subdir_name` の異常系拒否も確認。

## 非コミット方針
変換アセット（`video/`）と `__pycache__/` は `.gitignore` 済み。ポケモン素材と同じ
「実行時のみ・非コミット」（商標配慮）。

## 次の一歩（Step2）
端末側 SD 再生。手順・行アンカー・小さく刻む分割は PLAN.md「Step 2」に具体化済み。
まず Issue 起票 → SD 初期化 + 1 フレーム表示（2a）から。
