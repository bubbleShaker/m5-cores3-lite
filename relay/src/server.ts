// Hono 製の中継サーバ本体（実機依存に相当する「副作用あり」の層）。
// 純粋ロジックは chat.ts に分離済みなので、ここは「環境変数→Claude 呼び出し→整形」だけ。
import { Hono } from "hono";
import { serve } from "@hono/node-server";
import Anthropic from "@anthropic-ai/sdk";
import { SYSTEM_PROMPT, buildMessages, parseClaudeReply } from "./chat";
import {
  adjustAudioQuery,
  audioQueryUrl,
  parseTtsRequest,
  synthesisUrl,
} from "./tts";

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

// VOICEVOX ENGINE の所在。docker run -p 50021:50021 voicevox/voicevox_engine:cpu-latest を想定。
const VOICEVOX_URL = process.env.VOICEVOX_URL ?? "http://localhost:50021";

app.post("/tts", async (c) => {
  // 入力検証＋話者解決は純粋ロジックへ委譲。投げられたら 400。
  const body = await c.req.json().catch(() => null);
  let req: ReturnType<typeof parseTtsRequest>;
  try {
    req = parseTtsRequest(body);
  } catch (err) {
    return c.json({ error: (err as Error).message }, 400);
  }

  try {
    // ① 音の設計図（audio_query）を貰う。text/speaker はクエリ文字列で渡す仕様。
    const aqRes = await fetch(
      audioQueryUrl(VOICEVOX_URL, req.text, req.speaker),
      { method: "POST" },
    );
    if (!aqRes.ok) {
      console.error("voicevox audio_query failed:", aqRes.status);
      return c.json({ error: "voicevox audio_query failed" }, 502);
    }
    const query = (await aqRes.json()) as Record<string, unknown>;

    // ② 出力フォーマットを 16kHz/モノラルへ整形してから合成。
    const synRes = await fetch(synthesisUrl(VOICEVOX_URL, req.speaker), {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(adjustAudioQuery(query)),
    });
    if (!synRes.ok) {
      console.error("voicevox synthesis failed:", synRes.status);
      return c.json({ error: "voicevox synthesis failed" }, 502);
    }

    // WAV バイト列をそのまま audio/wav で返す。
    const wav = await synRes.arrayBuffer();
    return c.body(wav, 200, { "content-type": "audio/wav" });
  } catch (err) {
    console.error("voicevox call failed:", err);
    return c.json({ error: "upstream VOICEVOX call failed" }, 502);
  }
});

const port = Number(process.env.PORT ?? 3000);
serve({ fetch: app.fetch, port });
console.log(`relay listening on http://localhost:${port}`);

export default app;
