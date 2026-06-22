// Hono 製の中継サーバ本体（実機依存に相当する「副作用あり」の層）。
// 純粋ロジックは chat.ts に分離済みなので、ここは「環境変数→Claude 呼び出し→整形」だけ。
import { Hono } from "hono";
import { serve } from "@hono/node-server";
import Anthropic from "@anthropic-ai/sdk";
import { SYSTEM_PROMPT, buildMessages, parseClaudeReply } from "./chat";

// Node 22+ 標準機能で .env を読み込む（dotenv 依存を増やさない）。
// 無ければ実環境の環境変数をそのまま使う。
try {
  process.loadEnvFile();
} catch {
  // .env が無いだけ。CI や本番では実環境変数を使う想定。
}

// 軽量・低レイテンシなのでアバターの即応チャットには Haiku が好適。
const MODEL = "claude-haiku-4-5-20251001";

const app = new Hono();

app.get("/health", (c) => c.json({ ok: true }));

app.post("/chat", async (c) => {
  const body = await c.req.json().catch(() => null);
  if (!body || typeof body.message !== "string" || body.message.trim() === "") {
    return c.json({ error: "message (non-empty string) is required" }, 400);
  }

  const apiKey = process.env.ANTHROPIC_API_KEY;
  if (!apiKey) {
    return c.json({ error: "server missing ANTHROPIC_API_KEY" }, 500);
  }

  try {
    const client = new Anthropic({ apiKey });
    const res = await client.messages.create({
      model: MODEL,
      max_tokens: 512,
      system: SYSTEM_PROMPT,
      messages: buildMessages(body.message),
    });
    // content は複数ブロックになり得るのでテキストブロックだけ連結する。
    const text = res.content
      .filter((b): b is Anthropic.TextBlock => b.type === "text")
      .map((b) => b.text)
      .join("");
    return c.json(parseClaudeReply(text));
  } catch (err) {
    console.error("claude call failed:", err);
    return c.json({ error: "upstream Claude call failed" }, 502);
  }
});

const port = Number(process.env.PORT ?? 3000);
serve({ fetch: app.fetch, port });
console.log(`relay listening on http://localhost:${port}`);

export default app;
