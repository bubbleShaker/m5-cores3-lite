# relay — アバター中継サーバ（②-2b）

テーマK AIアバターのクラウド対話（案B: 中継サーバ経由）の中継サーバ。
デバイスに API キーを載せないため、このサーバが `ANTHROPIC_API_KEY` を保持して Claude を呼ぶ。

## エンドポイント

| メソッド | パス | リクエスト | レスポンス |
|---------|------|-----------|-----------|
| `POST` | `/chat` | `{ "message": "..." }` | `{ "reply", "expression", "action" }` |
| `GET`  | `/health` | — | `{ "ok": true }` |

- `expression`: `neutral` / `happy` / `thinking` / `sad` / `surprised`（アバターの語彙と一致）
- `action`: `none` / `notify`

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

## 設計

- `src/chat.ts` — 純粋ロジック（プロンプト組み立て・応答パース＆検証）。ネットワーク非依存で単体テスト可能。
- `src/server.ts` — Hono サーバ。環境変数読込→Claude 呼び出し→整形のみ。

## スコープ外（後続）

- クラウドデプロイ（AWS Lambda / Cloudflare Workers）
- デバイス側 HTTP クライアント＆応答表示（②-2c）
