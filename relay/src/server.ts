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
import { asrUrl, parseAsrText, parseSttOptions, validateAudio } from "./stt";
import {
  extractPokemonInfo,
  parsePokemonId,
  pokemonUrl,
  speciesUrl,
  type PokemonInfo,
} from "./pokemon";

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

// 自前ホスト Whisper(docker) の所在。
// docker run -d -p 9000:9000 onerahmet/openai-whisper-asr-webservice:latest を想定。
const STT_URL = process.env.STT_URL ?? "http://localhost:9000";

app.post("/stt", async (c) => {
  // オプション（language/task）はクエリ文字列から拾う。音声本体は raw body で来るため。
  let opts: ReturnType<typeof parseSttOptions>;
  try {
    opts = parseSttOptions(c.req.query());
  } catch (err) {
    return c.json({ error: (err as Error).message }, 400);
  }

  // raw WAV body を読み、純粋ロジックで WAV ヘッダ/容量を検証してから上流へ。
  const audio = new Uint8Array(await c.req.arrayBuffer());
  try {
    validateAudio(audio);
  } catch (err) {
    return c.json({ error: (err as Error).message }, 400);
  }

  try {
    // Whisper ASR webservice は multipart の audio_file フィールドで音声を受け取る。
    const form = new FormData();
    form.append("audio_file", new Blob([audio], { type: "audio/wav" }), "audio.wav");

    const res = await fetch(asrUrl(STT_URL, opts), { method: "POST", body: form });
    if (!res.ok) {
      console.error("whisper /asr failed:", res.status);
      return c.json({ error: "whisper /asr failed" }, 502);
    }
    // output=json なので JSON で受ける。壊れていても parseAsrText が空文字に丸める。
    const json = await res.json().catch(() => null);
    return c.json({ text: parseAsrText(json) });
  } catch (err) {
    console.error("whisper call failed:", err);
    return c.json({ error: "upstream Whisper call failed" }, 502);
  }
});

// PokeAPI の所在。公式ホスト。fair-use ポリシーに従い下でオンメモリキャッシュする。
const POKEAPI_URL = process.env.POKEAPI_URL ?? "https://pokeapi.co/api/v2";

// 図鑑番号 → コンパクト情報のオンメモリキャッシュ。
// PokeAPI の "Locally cache resources" ポリシー順守＋レイテンシ削減。
// プロセス再起動でクリア（永続化しない = ライセンス方針にも合致）。
const pokemonInfoCache = new Map<number, PokemonInfo>();

app.get("/pokemon/info/:id", async (c) => {
  // 図鑑番号の検証は純粋ロジックへ委譲。不正なら 400。
  let id: number;
  try {
    id = parsePokemonId(c.req.param("id"));
  } catch (err) {
    return c.json({ error: (err as Error).message }, 400);
  }

  // キャッシュヒットなら PokeAPI を叩かず即返す。
  const cached = pokemonInfoCache.get(id);
  if (cached) {
    return c.json(cached);
  }

  try {
    // pokemon（個体データ）と pokemon-species（多言語名・分類・説明）を並行取得。
    const [pokeRes, specRes] = await Promise.all([
      fetch(pokemonUrl(POKEAPI_URL, id)),
      fetch(speciesUrl(POKEAPI_URL, id)),
    ]);
    if (!pokeRes.ok || !specRes.ok) {
      console.error("pokeapi fetch failed:", pokeRes.status, specRes.status);
      return c.json({ error: "upstream PokeAPI fetch failed" }, 502);
    }

    const pokemon = (await pokeRes.json()) as Record<string, unknown>;
    const species = (await specRes.json()) as Record<string, unknown>;

    // ~30KB の生 JSON を実機向けコンパクト JSON へ純粋関数で削る。
    const info = extractPokemonInfo(id, pokemon, species);
    pokemonInfoCache.set(id, info);
    return c.json(info);
  } catch (err) {
    console.error("pokeapi call failed:", err);
    return c.json({ error: "upstream PokeAPI call failed" }, 502);
  }
});

const port = Number(process.env.PORT ?? 3000);
serve({ fetch: app.fetch, port });
console.log(`relay listening on http://localhost:${port}`);

export default app;
