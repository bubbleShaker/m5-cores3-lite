# relay — アバター中継サーバ（②-2b）

テーマK AIアバターのクラウド対話（案B: 中継サーバ経由）の中継サーバ。
デバイスに API キーを載せないため、このサーバが `ANTHROPIC_API_KEY` を保持して Claude を呼ぶ。

## エンドポイント

| メソッド | パス | リクエスト | レスポンス |
|---------|------|-----------|-----------|
| `POST` | `/chat` | `{ "message": "..." }` | `{ "reply", "expression", "action" }` |
| `POST` | `/tts` | `{ "text": "...", "voice_id"?: 3 }` | `audio/wav`（16kHz/モノラル） |
| `POST` | `/stt` | raw WAV body（`?language=ja&task=transcribe`） | `{ "text": "..." }` |
| `GET`  | `/health` | — | `{ "ok": true }` |

- `expression`: `neutral` / `happy` / `thinking` / `sad` / `surprised`（アバターの語彙と一致）
- `action`: `none` / `notify`
- `/tts` の `voice_id` は VOICEVOX の話者 ID（既定 `3` = ずんだもんノーマル）
- `/stt` は音声を raw WAV body で受け取る。`language`（既定 `ja`）/ `task`（`transcribe` 既定・`translate` 可）はクエリで指定

## セットアップ（初回のみ）

```sh
cd relay
npm install
cp .env.example .env   # .env を編集して ANTHROPIC_API_KEY を入れる（.env は gitignore 済み）
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

## 設計

- `src/chat.ts` — 純粋ロジック（プロンプト組み立て・応答パース＆検証）。ネットワーク非依存で単体テスト可能。
- `src/tts.ts` — 純粋ロジック（入力検証・話者解決・audio_query 整形・URL 組み立て）。
- `src/stt.ts` — 純粋ロジック（オプション検証・WAV 検証・/asr URL 組み立て・レスポンス解析）。
- `src/server.ts` — Hono サーバ。環境変数読込→Claude / VOICEVOX / Whisper 呼び出し→整形のみ。

VOICEVOX 生成音声は話者ごとの規約に従い、配布時は「VOICEVOX:ずんだもん」等のクレジットを明記すること。音声アセットはコミットしない（実行時生成）。

## スコープ外（後続）

- クラウドデプロイ（AWS Lambda / Cloudflare Workers）
- デバイス側 HTTP クライアント＆応答表示（②-2c）
- M3b（実機）: M5.Mic 録音→WAV→`/stt`→`/chat`→`/tts` の一巡（聞いて→考えて→喋る）
