# relay — アバター中継サーバ（②-2b）

テーマK AIアバターのクラウド対話（案B: 中継サーバ経由）の中継サーバ。
デバイスに API キーを載せないため、このサーバが `ANTHROPIC_API_KEY` を保持して Claude を呼ぶ。

## 認証（#216）

`/health` を除く**全エンドポイント**が共有トークンを要求する。
リクエストに `X-Relay-Token: <RELAY_TOKEN>` を付ける。一致しなければ `401 {"error":"unauthorized"}`。

```sh
curl -s -X POST localhost:3000/chat \
  -H "x-relay-token: $RELAY_TOKEN" \
  -H 'content-type: application/json' \
  -d '{"message":"おはよう"}'
```

- `RELAY_TOKEN` が未設定、または 16 文字未満なら**サーバは起動しない**。
  設定を忘れた時に無認証で立ち上がる方が危険なため（フェイルクローズ）。
- 実機側は `src/secrets.h` の `RELAY_TOKEN` に同じ値を入れる（`.gitignore` 済み）。
- 認証は**許可リスト方式**（`src/auth.ts` の `PUBLIC_PATHS`）。エンドポイントを追加すると
  既定で保護されるので、付け忘れが穴にならない。

> なぜ要るか: relay は `ANTHROPIC_API_KEY` を持ったまま LAN に口を開けている。無認証だと
> 同じ Wi-Fi の誰でもそのキーで課金でき、`/pokemon/*` は外部 CDN への踏み台になる。

### 既知の限界（この認証で防げないこと）

`RELAY_URL` は `http://` なので、**トークンは平文で LAN を流れる**。同じアクセスポイントに
いる攻撃者は通信を覗いてトークンを拾い、そのまま再利用できる（リプレイ）。
つまりこの認証が守るのは「通信を盗聴していない第三者」までで、盗聴者は防げない。

現状は「LAN 内の他人が偶然/故意に叩く」を塞ぐのが目的なので許容している。

**#219（PC 操作の実行層）を入れた時点での再評価:** 盗聴者がトークンを拾えば
**その PC のアプリを起動・操作できる**ので、影響は「API キーの悪用」から一段上がった。
ただし被害の上限は `apps.json` に列挙したアプリを起動・最小化できるところまでで、
引数は渡せず、任意コマンド実行やファイル操作には広がらない（実行層の不変条件）。
そのうえで次の 2 つで運用上のリスクを抑えている:

- 実行は既定で無効（`RELAY_DRY_RUN=0` を明示した時だけ動く）
- `apps.json` を置かなければ操作機能ごと無効

HTTPS 化・署名付きリクエストへの移行は別 Issue（#226）で扱う。

## エンドポイント

| メソッド | パス | リクエスト | レスポンス | 認証 |
|---------|------|-----------|-----------|------|
| `POST` | `/chat` | `{ "message": "..." }` | `{ "reply", "expression", "action" }` | 要 |
| `POST` | `/tts` | `{ "text": "...", "voice_id"?: 3 }` | `audio/wav`（16kHz/モノラル） | 要 |
| `POST` | `/stt` | raw WAV body（`?language=ja&task=transcribe`） | `{ "text": "..." }` | 要 |
| `GET`  | `/pokemon/info/:id` | — | コンパクト JSON | 要 |
| `GET`  | `/pokemon/sprite/:id` | — | RGB565 raw（18432B） | 要 |
| `GET`  | `/pokemon/cry/:id` | — | `audio/wav` | 要 |
| `GET`  | `/health` | — | `{ "ok": true }` | 不要 |

- `expression`: `neutral` / `happy` / `thinking` / `sad` / `surprised`（アバターの語彙と一致）
- `action`: `none` / `notify`
- `/tts` の `voice_id` は VOICEVOX の話者 ID（既定 `3` = ずんだもんノーマル）
- `/stt` は音声を raw WAV body で受け取る。`language`（既定 `ja`）/ `task`（`transcribe` 既定・`translate` 可）はクエリで指定

## セットアップ（初回のみ）

```sh
cd relay
npm install
cp .env.example .env   # .env を編集して RELAY_TOKEN と ANTHROPIC_API_KEY を入れる（.env は gitignore 済み）
```

## 実行・テスト

```sh
npm run dev        # ローカル起動（http://localhost:3000）
npm test           # 純粋ロジックの単体テスト（API キー不要）
npm run typecheck  # 型チェック
```

## 動作確認（要 API キー）

```sh
curl -s localhost:3000/health
curl -s -X POST localhost:3000/chat \
  -H 'content-type: application/json' \
  -d '{"message":"おはよう"}'
```

## 動作確認 `/tts`（要 VOICEVOX、API キー不要）

```sh
# 別ターミナルで VOICEVOX ENGINE を起動
docker run --rm -p 50021:50021 voicevox/voicevox_engine:cpu-latest

# WAV を取得して再生できることを確認
curl -s -X POST localhost:3000/tts \
  -H 'content-type: application/json' \
  -d '{"text":"こんにちは"}' -o out.wav
```

VOICEVOX が別ホスト/ポートなら `VOICEVOX_URL`（既定 `http://localhost:50021`）で上書きする。

## 動作確認 `/stt`（要 Whisper、API キー不要・実機不要）

```sh
# 別ターミナルで自前ホスト Whisper を起動
docker run -d -p 9000:9000 onerahmet/openai-whisper-asr-webservice:latest

# WAV を文字起こしできることを確認（sample.wav は任意の音声 WAV）
curl -s -X POST localhost:3000/stt \
  -H 'content-type: audio/wav' \
  --data-binary @sample.wav
# => {"text":"..."}
```

Whisper が別ホスト/ポートなら `STT_URL`（既定 `http://localhost:9000`）で上書きする。

## 声で PC を操作する（#218 / #219）

`/chat` の応答に、検証済みの操作 `tool` が入る。

```json
{"reply":"ブラウザ開くね","expression":"happy","action":"none",
 "tool":{"name":"launch_app","app":"browser"}}
```

**既定は dry-run（PC に一切触れない）。** 実行するには `RELAY_DRY_RUN=0` を明示的に指定する。
声で PC が動くのは戻せない副作用なので、opt-in でしか有効にしない。

```sh
RELAY_DRY_RUN=0 npm start   # 実際にアプリを起動・ウィンドウ操作する
```

返答文（`reply`）は**実際に起きたこと**で上書きする。LLM は操作できたつもりで「開いたよ」と
書いてくるので、そのまま喋らせると**何も起きていないのに成功したと嘘をつく**ことになるため。

| 結果 | 喋る文 |
|---|---|
| 成功 | `browser を開いたのだ。` |
| 拒否（未登録・未知のツール） | `それはできないのだ。` |
| 失敗（起動できない等） | `うまくいかなかったのだ。` |
| 窓が無い（まだ起動していない） | `まだ開いていないのだ。` |
| dry-run | `その操作はまだできないのだ。` |

### 設計の一線: LLM にシェルの文字列を作らせない

選ばせるのは「列挙された関数名 + 型検証済みの引数」だけ。

```
NG: {"command": "start chrome.exe && rm -rf ..."}
OK: {"name": "launch_app", "args": {"app": "browser"}}
```

STT は聞き間違え、LLM は幻覚を出す。前段が 2 つとも確率的なので「正しく動くこと」に
賭けた設計は必ず破れる。列挙型に閉じ込めれば、どちらが暴走しても**定義外の操作は起き得ない**。
不正な `tool` は理由をログに残して `reply_only`（何もしない）へ落ちる。

### 使えるツール

| name | args | 動作 |
|---|---|---|
| `launch_app` | `app` | 登録済みアプリを起動 |
| `focus_window` | `app` | 開いている窓へ切り替え |
| `window_state` | `app`, `state`（`minimize`/`maximize`/`restore`） | 窓の状態を変える |
| `reply_only` | — | 操作せず喋るだけ |

### apps.json（操作してよいアプリの一覧）

```sh
cp apps.example.json apps.json   # apps.json は gitignore 済み
```

```json
{ "browser": "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" }
```

- **LLM が指定できるのはキー（`browser`）だけ**で、実行パスはこのファイルにしか存在しない。
- キーは `a-z 0-9 _ -` の 32 文字までに制限する（プロンプトとログに出るため）。
- ファイルが無ければ「操作は無効・対話のみ」で起動する。**壊れている場合は起動を止める**
  （設定ミスに気づかないまま「なぜか操作できない」と悩む方が損失が大きいため）。
- `RELAY_APPS_FILE` で別の場所を指せる。
- **実行パスは絶対パスの `.exe` だけ**（#219）。次のいずれかに当たると**起動を止める**:
  相対パス / `..` を含む / `%WINDIR%` や `$env:` などの展開記法 / UNC(`\\server\share`) /
  `.bat` `.cmd` `.ps1` `.lnk` / シェル・スクリプトホスト
  （`cmd.exe` `powershell.exe` `wscript.exe` `mshta.exe` 等）。

> なぜ `.bat` / `.cmd` を許さないか: Node は Windows でバッチファイルを**シェル経由でしか
> 起動できない**（CVE-2024-27980 の緩和）。許した瞬間に「シェルを介さない」という前提が崩れる。

### 実行層（#219）

| ファイル | 役割 |
|---|---|
| `src/winexec.ts` | 純粋。実行パスの検証・プロセス名の導出・PowerShell 引数配列の組み立て |
| `src/executor.ts` | **唯一 OS に触る層**。`spawn`（`shell:false`）で起動、失敗は全て `outcome` へ写像 |
| `scripts/window.ps1` | ウィンドウ操作（user32 の `ShowWindow` / `SetForegroundWindow`） |
| `src/audit.ts` | 純粋。監査ログ 1 行の整形 |

守っている不変条件:

- **シェルを介さない。** `exec` 系は使わず `spawn(exe, [], { shell: false })`。
  PowerShell も `-Command` ではなく **`-File` + パラメータ束縛**で呼ぶので、渡した値は
  コードではなく文字列として入る。
- **アプリへ渡す引数は常に空配列。** ここに口を作ると許可リストが無意味になる
  （`terminal` に `-c ...` を渡せたら任意コマンド実行と同じ）。
- **許可リストは二重に持つ。** `tools.ts`（LLM 入力の検証）と `executor.ts`（実行直前の再検証）。
  片方を書き換えても穴が開かない。
- **例外を外へ出さない。** 起動失敗・タイムアウト・窓が無い、は全て日本語の返答へ写像する。

環境変数:

| 変数 | 既定 | 意味 |
|---|---|---|
| `RELAY_DRY_RUN` | `1` | `0` で実行を有効化。既定は PC に触れない |
| `RELAY_EXEC_TIMEOUT_MS` | `10000` | PowerShell が無応答の時に諦めるまで |

> ⚠ `RELAY_DRY_RUN=0` は**信頼できる LAN でのみ**。トークンは平文で流れるので、
> 公衆 Wi-Fi では盗聴者が同じ操作を再実行できる（#226）。

`window.ps1` の終了コード:

| コード | 意味 | 返す outcome |
|---|---|---|
| 0 | 成功 | `ok` |
| 3 | 対象の窓が無い | `not_running` |
| その他 | 失敗（引数の束縛失敗を含む） | `failed` |

既知の制約:

- **ウィンドウの特定はプロセス名 + 実行パス。** `Get-Process -Name` はベース名一致なので、
  実行パスが一致するものがあればそこまで絞る。一致が 0 件なら名前一致へフォールバックする
  ── Windows 11 の `notepad.exe` のように**起動したパスと実プロセスのパスが違う**アプリが
  あるため（Store 版へリダイレクトされ実体は `WindowsApps\...\Notepad.exe`）。
  フォールバック時は同名の別プロセスの窓も動き得るが、影響は最小化/最大化/前面化まで。
- **`launch_app` の `ok` は「起動できた」まで。** 直後にアプリが自分で落ちても成功と答える。
- **同時に走る操作は 1 つ。** 実行中の操作要求は `denied`（プロセスの無制限増殖を防ぐため）。
- **監査ログは stdout に出すだけ。** ローテーション・永続化は運用側の責務。

監査ログ（1 行 1 レコードの JSON。`grep` / `jq` で追える）:

```
audit {"time":"2026-07-26T09:00:00.000Z","utterance":"ブラウザ最小化して","tool":"window_state","outcome":"ok","app":"browser","state":"minimize"}
```

> `scripts/window.ps1` は **UTF-8 BOM 付き**で保存すること。Windows PowerShell 5.1 は
> BOM の無いファイルを ANSI(cp932) として読むため、日本語コメントが化けて構文エラーになる。

## 設計

- `src/chat.ts` — 純粋ロジック（プロンプト組み立て・応答パース＆検証）。ネットワーク非依存で単体テスト可能。
- `src/auth.ts` — 純粋ロジック（トークン検証・定数時間照合・認証免除パスの判定）。
- `src/tools.ts` — 純粋ロジック（apps.json の検証・ツール呼び出しの検証・喋る文の整形）。OS も fs も触らない。
- `src/winexec.ts` / `src/audit.ts` — 純粋ロジック（実行パス検証・PowerShell 引数組み立て・監査ログ整形）。
- `src/executor.ts` — 実行アダプタ（#219）。**唯一 OS に触る層**で、純粋ロジックからは依存されない。
- `src/tts.ts` — 純粋ロジック（入力検証・話者解決・audio_query 整形・URL 組み立て）。
- `src/stt.ts` — 純粋ロジック（オプション検証・WAV 検証・/asr URL 組み立て・レスポンス解析）。
- `src/server.ts` — Hono サーバ。環境変数読込→Claude / VOICEVOX / Whisper 呼び出し→整形のみ。

VOICEVOX 生成音声は話者ごとの規約に従い、配布時は「VOICEVOX:ずんだもん」等のクレジットを明記すること。音声アセットはコミットしない（実行時生成）。

## スコープ外（後続）

- クラウドデプロイ（AWS Lambda / Cloudflare Workers）
- デバイス側 HTTP クライアント＆応答表示（②-2c）
- M3b（実機）: M5.Mic 録音→WAV→`/stt`→`/chat`→`/tts` の一巡（聞いて→考えて→喋る）
