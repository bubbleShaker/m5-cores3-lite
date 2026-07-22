# PLAN: 動画再生シーン（SD フレーム再生）

このファイルは動画再生機能の段取りを永続化するもの。会話の要約で消えないよう、次セッションはここから再開する。

## 次セッションの開始点（ここから読む）

### ✅ パック方式で再生速度は解決した（#170・2026-07-21 実測）
**全長 2,355 フレームが最後まで 100ms 未満で描け、絵と音のズレも 235 秒で 8ms に収まった。**
`frames.bin`（先頭に固定長8バイトの索引、その後に JPEG を連結）を入場時に1回だけ open し、
以降は索引を引いて seek→read→`drawJpg(buf,len)` する方式。ファイル名の解決が原理的に消えた。

| フレーム番号 | フラット配置（旧） | パック方式（#170） |
|---|---|---|
| idx=0 | 58ms | **46ms** |
| idx=1600 | **1,051ms** | **65ms** |
| idx=2200 | （実質再生不能） | **81ms** |

- `draw total` は全域 46〜81ms で、**フレーム番号が進んでも増えない**（＝走査が消えた証拠）
- 1周の実時間 235,512ms vs 音声の公称 235,520ms → **ズレ 8ms（0.003%）**
- 入退場を4回繰り返しても `audio-alloc-before` が完全に同一値（free=8,308,231 /
  largest=8,257,524）。**リークも断片化もゼロ**（reviewer が懸念した確保順/解放順の件は実機で否定）
- MSC 転送も 85KB/s → **204KB/s**（33.6MB を 164.6 秒）。小ファイル 2,355 個のオーバーヘッドが消えた

**PSRAM の占有は音声の 7.5MB だけ**（動画シーンに居る間のみ。退場で全部返る）。
映像側は索引 18.8KB + JPEG バッファ 16.6KB の計 35KB しか使わない（39MB を載せずに SD から seek）。
減らす必要が出たら `--sample-rate 8000` で 3.7MB に半減できる。

⚠ **サブディレクトリ分割は実装して実機で測り、逆効果と確認して破棄した。二度やらないこと。**
`idx=0 で 4ms` を「走査が短いから速い」と読んだのが誤りで、あれは frame_00001.jpg が
ディレクトリの先頭エントリだった最良値にすぎなかった。実際は**パスの階層を1つ増やすコスト自体**が
大きく（ディレクトリを開くたび FAT のクラスタ連鎖を辿り SPI 往復が発生）、走査短縮の利得を上回る。

### 🔴 次にやること（Issue は起票済み）

**#176 30fps 化の可否を判断する（まず内訳を実測）**
30fps の予算は 33.3ms だが、現在は最良値 46ms で**既に予算超過**。ただし `draw total` の内訳
（SD読み / JPEGデコード / LCD転送）を測っていないので、どこを削れば効くかが分からない。
⚠ **推測で解像度を下げに行かないこと。** #169 で実測値の解釈を誤ってサブディレクトリ分割という
逆効果な実装をした前科がある。まず測る。

未検証の仮説: **LCD と SD が同一 SPI バス**（#157）なので転送時間は足し算になる。全画面
320×240×16bit = 153,600B の転送だけで 40MHz なら約 31ms＝30fps 予算の 92%。もしそうなら
**全画面のままの 30fps は原理的に不可能**で、解像度を落とすしかない。LCD の実クロックは
M5GFX 内部で決まっており自前コードに現れないため、これも確認対象（SD は 25MHz と判明済み）。

なお **30fps が無理でも実害は無い**（現状 10fps で体感上の問題なし）。その場合は
「全画面では 12〜21fps が上限」という事実を記録して閉じてよい。

### ✅ #175 動画選択（実装完了・実機確認済み 2026-07-22）
`kVideoDir` 固定を廃し、`/video/` 直下を列挙して選択画面から選ぶようにした。純粋ロジック
`src/video_list.*`（名前検証・候補リスト・カーソル巡回・native テスト）＋端末側の2状態
（選択→再生）。操作は左タップ=次へ／右タップ=決定（長押しは既存どおりメニューへ戻る固定）。
壊れた動画は選択画面へ戻して理由表示。素材 `video/lain/`（Lain OP・1016フレーム）を変換済み。
詳細は `summary/260722/01-video-select.md`。
実機確認済み: SD に `bluelight`（全長 2355 フレーム）と `lain` の 2 本を置き、選択画面から切り替えて
再生できることを確認した。SD 上の `sample` は `kVideoDir` 固定時代の名残の名前だったので `bluelight`
へリネームした（中身は無変更）。PSRAM 返却は目視のみでログ未取得（数値の裏取りは未了）。

### ✅ 音声は無実（#169 で確定）
`millis` と I2S のクロック差を疑ったが、実測で音声は 235 秒に対し誤差 ±60ms（0.025%）と正確だった。
実時刻基準（`video_frame_at`）の設計も正しく機能している。問題は時間軸ではなく
**画面に出る絵の鮮度**（描画に1秒かかれば1秒古い絵を見ることになる）。

⚠ ログの値は「実際に起きた時刻」ではなく**「気づいた時刻」**。初回測定で音声誤差が +1452ms に
見えたのは、音声終了と周回検知が同じループ周回で拾われたため。検知の遅れと実誤差を混同しないこと。


### ✅ microSD 入手済み・転送経路も実証済み（2026-07-20）
29.1GB FAT32 のカードを本体に装着済み。MSC 経由で PC から D: ドライブとして読み書きでき、
`/video/sample/`（30秒クリップ・300フレーム・音声960KB）を転送して**実機再生に成功**した。
以降は下記「アセットの転送手順（実証済み）」を使い回す。

### ✅ 全長素材は PSRAM に載った → 2c-2 は現時点で不要（2026-07-20・#166）
全長 236 秒（2,355フレーム / 39MB）を転送して実測した。

```
want           = 7,536,718 B (7.19 MiB)
確保前: free   = 8,343,743 B / largest = 8,257,524 B (7.87 MiB)
確保後: free   =   807,007 B / largest =   802,804 B  (784 KiB)
音声          = 3,768,320 samples @16000Hz mono = 235.5 秒（全長）
```

**余裕は約 687KiB。** よって **2c-2（チャンクストリーミング）は作らなくてよい**。

### 🔴 ただし上の測定は最良条件（追試が #168）
ログ先頭に `ESP-ROM` / `[boot]` が出ており、**電源投入直後**の測定だった。確保前の largest が
PSRAM 全体の 98% ＝ ほぼ手つかずの状態。**#128（フルスクリーン Sprite が解放されず常駐）** が
未解決なので、他シーンを経由してから動画再生に入ると入らなくなる可能性がある。
余裕 687KiB に対しフルスクリーンスプライトは 1 枚 約150KB なので、数枚残るだけで危うい。
**追試は #168**（実装不要・実機操作のみ。診断ログは #166 で入れてある）。
失敗するようなら #128 の優先度を上げるか、`--sample-rate 8000`（約3.6MiB に半減）で回避する。

### ✅ 実機は復旧済み（2026-07-18・#159 クローズ）
通常ファームに焼き戻し済みで、COM3 で正常に書き込める状態。以下は再発時の手順。

ダウンロードモードへの入り方:
1. **底面の RST ボタンを 3 秒長押し**
2. `platformio run -e m5stack-cores3 -t upload`

**緑 LED は当てにしない**。ハードウェア機能なのでファームが壊れていても動くが、"internal" LED
なので外から見えないことがある（実際 LED 未確認のまま焼き込みに成功した）。判定はこれで:
```powershell
Get-PnpDevice | Where-Object InstanceId -like '*VID_303A*' | Select-Object Status,Class,FriendlyName
```
`USB JTAG/serial debug unit` が出ていれば入れている。迷うより upload を試す方が早い。

⚠ **ボタンを間違えないこと**（実際に間違えて時間を溶かした）:

| ボタン | 位置 | 操作 | 動作 |
|---|---|---|---|
| POWER | **左側面** | シングルクリック | 電源オン |
| POWER | **左側面** | 6 秒長押し | **電源オフ** |
| RST | **底面** | シングルクリック | リセット |
| RST | **底面** | **3 秒長押し** | **ダウンロードモード**（緑 LED） |

左側面を長押しすると電源が落ちるだけ。COM ポートが全部消えたらそれを疑う。
出典: https://docs.m5stack.com/en/core/CoreS3

### アセットの転送手順（2026-07-20 に実証済み・毎回この順で回す）

⚠ **ポート番号は状態で変わる**。ini の `upload_port` を当てにせず、毎回 `--upload-port` で明示すること。
実測では 通常ファーム=COM3 / MSC ファーム稼働中=COM5（ini の COM4 は当時の値で、今は合っていない）。

0. ⚠ **SD に前の素材が残っていたら先に消す**。実測でサブディレクトリ 24 個の削除に 95 秒かかった
   （小ファイル 2,355 個の削除は転送と同じくらい遅い）。robocopy は既存を消さないので混在する。

1. **アセットを変換**（PC のみ・実機不要）
   - `python tools/video2frames.py <URL または動画ファイル> --name <素材名>`
   - #175 以降、名前は自由（`/video/<素材名>/` として選択画面に出る）。素材の実体が分かる名前にする。
   - 既定でパック方式（`frames.bin` 1 本 + `audio.wav` + `meta.txt` の計 3 ファイル）。
     `--no-pack` で従来の連番も出せるが、端末では遅い経路になるので通常は使わない。
   - 尺を絞りたい場合は先に `ffmpeg -y -i source.webm -t 30 -c:v libx264 -preset veryfast -c:a aac clip30.mp4`
     で切り出してから渡す（tools には `--duration` が無い）。**検証ループを速く回すには短尺が有利**。
2. **MSC ファームを焼く**（通常ファームが動いている状態から焼くので自動書き込みが効く）
   - `pio run -e m5stack-cores3-msc -t upload --upload-port COM3`
3. **転送**
   - リムーバブルドライブとして現れる（実測 D: / 29.1GB FAT32）
   - `robocopy video\<素材名> D:\video\<素材名> /E` （終了コード 1 は「コピー成功」。エラーではない）
   - ⚠ **SD 上の既存ディレクトリを同名のローカル素材で上書きしない**。ローカルの `video/` には
     過去の変換物が同名で残っていることがあり、中身が別物のまま名前だけ一致している場合がある。
   - `Write-VolumeCache -DriveLetter D` で書き込みキャッシュを流す
4. **通常ファームへ焼き戻す** — ⚠ **ここは必ず手作業が要る**（下記参照）
   - **底面 RST を 3 秒長押し**してダウンロードモードへ入れる
   - `pio run -e m5stack-cores3 -t upload --upload-port COM3`

#### ⚠ MSC ファームからの自動リセットは効かない（2026-07-20 実測・確定）
PLAN に「`USB.begin()` を全経路で呼ぶので効く可能性がある」と書いていたが、**実測で否定された**。
MSC ファーム稼働中に `-e m5stack-cores3 -t upload` すると
`A fatal error occurred: Failed to connect to ESP32-S3: No serial data received.` で失敗する。
つまり **TinyUSB 化した時点で自動リセットは効かない**（`USB.begin()` の有無は関係ない）。
焼き戻しには毎回 **底面 RST の 3 秒長押し**が必要。これは MSC 経路を使う限り避けられない固定コスト。

### 実測で分かったこと（2026-07-20・#154）
- 30秒クリップ（300フレーム・10fps・音声 16kHz mono 960KB）で**絵も音も正常に再生**できた。
- **時間軸は正しい**（約30秒で一巡）。実時刻基準の設計が効いている。
- 発見した不具合 → **#164**: 絵はループするが音は2周目以降鳴らなかった（`video_frame_at` は剰余で
  ループするのに `playRaw` は一発再生、という Step0 と 2c-1 の噛み合わせ不良）。修正済み。
- **drawJpgFile の実 fps は未計測**。動画シーンにシリアルログが無く測る手段が無い。
  必要になったら計装を入れる Issue を立てること。今のところ 10fps で体感上の問題は出ていない。

### 残りのタスク
1. **動画の選択画面**（上記 🔴）
2. **2d**（タップで一時停止/再開）。長押しメニュー復帰＆音停止は既存機構で対応済み
   （loop の Speaker.stop + videoExit）。

### ハマりどころ（#157 で判明・繰り返さないこと）
- **LCD と microSD は同じ SPI バス**。GPIO35 が SD の MISO と LCD の D/C の兼用。
  別タスクから SD と画面を同時に触ると書き込みデータが化ける（静かなデータ破壊）。
  `main.cpp` が無事なのは全部 loop タスクで直列化されているからであって、バスが別だからではない。
  （`research/sd-video-playback.md` に「別バス」と書いていたのは誤り。訂正済み）
- **`ARDUINO_USB_MODE=0`（TinyUSB）にすると esptool の自動書き込みが効かなくなる**。
  `default_reset` / `usb_reset` とも失敗し、毎回ダウンロードモード操作が必要になる。
  そのため通常ファームは `USB_MODE=1` のまま残し、MSC は別 env に分離してある。

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
    - [ ] **2c-2（不要と判断）**: 全長 7.5MB が PSRAM に載り、ズレも 235 秒で 8ms なのでチャンク
          ストリーミングは作らない。必要になったら `--sample-rate 8000` で半減させる方が安い。
  - [x] **2e（完了・Issue #170）**: パック方式（frames.bin）。実測は冒頭の表を参照。
        純粋関数 `video_pack_entry`（索引読み取り＋範囲検証）と `meta_get_str`/`meta_has_key` を追加。
        `pack=` の無い旧アセットは従来の連番方式で再生する（互換）。
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
