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
**#219 で PC 操作を足す時にここを再評価する**（HTTPS 化、または署名付きリクエストへの移行）。

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

## 声で PC を操作する（#218・現在は dry-run）

`/chat` の応答に、検証済みの操作 `tool` が入る。

```json
{"reply":"ブラウザ開くね","expression":"happy","action":"none",
 "tool":{"name":"launch_app","app":"browser"}}
```

**⚠ 現時点では実行層が無い（#219）。** 決まった操作をログに出すだけで PC には一切触れない。
そのため操作を選んだ場合は `reply` を「その操作はまだできないのだ。」に差し替える。
LLM は操作できたつもりで「開いたよ」と書いてくるので、そのまま喋らせると
**何も起きていないのに成功したと嘘をつく**ことになるため。拒否した場合も同様に差し替える。

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
- `RELAY_DRY_RUN=0` で実行を有効化する予定（**実行層は #219 でまだ無い**）。

## 設計

- `src/chat.ts` — 純粋ロジック（プロンプト組み立て・応答パース＆検証）。ネットワーク非依存で単体テスト可能。
- `src/auth.ts` — 純粋ロジック（トークン検証・定数時間照合・認証免除パスの判定）。
- `src/tools.ts` — 純粋ロジック（apps.json の検証・ツール呼び出しの検証・喋る文の整形）。OS も fs も触らない。
- `src/tts.ts` — 純粋ロジック（入力検証・話者解決・audio_query 整形・URL 組み立て）。
- `src/stt.ts` — 純粋ロジック（オプション検証・WAV 検証・/asr URL 組み立て・レスポンス解析）。
- `src/server.ts` — Hono サーバ。環境変数読込→Claude / VOICEVOX / Whisper 呼び出し→整形のみ。

VOICEVOX 生成音声は話者ごとの規約に従い、配布時は「VOICEVOX:ずんだもん」等のクレジットを明記すること。音声アセットはコミットしない（実行時生成）。

## スコープ外（後続）

- クラウドデプロイ（AWS Lambda / Cloudflare Workers）
- デバイス側 HTTP クライアント＆応答表示（②-2c）
- M3b（実機）: M5.Mic 録音→WAV→`/stt`→`/chat`→`/tts` の一巡（聞いて→考えて→喋る）
