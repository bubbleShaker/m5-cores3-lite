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
  ttsCacheKey,
} from "./tts";
import { asrUrl, parseAsrText, parseSttOptions, validateAudio } from "./stt";
import {
  extractPokemonInfo,
  parsePokemonId,
  pokemonUrl,
  speciesUrl,
  type PokemonInfo,
} from "./pokemon";
import { rgbaToRgb565, SPRITE_SIZE, spriteUrl } from "./sprite";
import sharp from "sharp";
import {
  cryUrl,
  ffmpegArgs,
  MAX_CRY_BYTES,
  WAV_HEADER_SIZE,
  writeWavHeader,
} from "./cry";
import { runTranscode } from "./transcode";
import {
  fetchWithTimeout,
  isTimeoutError,
  mapUpstreamStatus,
  mapUpstreamStatusPair,
  TIMEOUT_LLM_MS,
  TIMEOUT_STATIC_MS,
  TIMEOUT_VOICE_MS,
} from "./upstream";

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
    // 上流ハング対策。SDK は timeout 超過で APIConnectionTimeoutError を投げる。
    const client = new Anthropic({ apiKey, timeout: TIMEOUT_LLM_MS });
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
    if (err instanceof Anthropic.APIConnectionTimeoutError) {
      return c.json({ error: "upstream Claude timed out" }, 504);
    }
    return c.json({ error: "upstream Claude call failed" }, 502);
  }
});

// VOICEVOX ENGINE の所在。docker run -p 50021:50021 voicevox/voicevox_engine:cpu-latest を想定。
const VOICEVOX_URL = process.env.VOICEVOX_URL ?? "http://localhost:50021";

// 解説文 → 合成 WAV のオンメモリキャッシュ（#98）。宝石/ポケの解説は少数の定型文なので、
// 2 回目以降は VOICEVOX を叩かず即返し、タップ→読み上げの体感遅延を消す。
// info/sprite/cry キャッシュと違いキーが任意文字列なので、件数上限を設けて有界化する
// （超過時は挿入順の最古を 1 件追い出す＝メモリ枯渇を防ぐ）。プロセス再起動でクリア・永続化しない。
const TTS_CACHE_MAX = 256;
const ttsCache = new Map<string, Uint8Array<ArrayBuffer>>();

app.post("/tts", async (c) => {
  // 入力検証＋話者解決は純粋ロジックへ委譲。投げられたら 400。
  const body = await c.req.json().catch(() => null);
  let req: ReturnType<typeof parseTtsRequest>;
  try {
    req = parseTtsRequest(body);
  } catch (err) {
    return c.json({ error: (err as Error).message }, 400);
  }

  // キャッシュヒットなら VOICEVOX を叩かず即返す（初回だけ合成コストを払う）。
  const cacheKey = ttsCacheKey(req.text, req.speaker);
  const cachedWav = ttsCache.get(cacheKey);
  if (cachedWav) {
    return c.body(cachedWav, 200, { "content-type": "audio/wav" });
  }

  try {
    // ① 音の設計図（audio_query）を貰う。text/speaker はクエリ文字列で渡す仕様。
    const aqRes = await fetchWithTimeout(
      audioQueryUrl(VOICEVOX_URL, req.text, req.speaker),
      TIMEOUT_VOICE_MS,
      { method: "POST" },
    );
    if (!aqRes.ok) {
      console.error("voicevox audio_query failed:", aqRes.status);
      return c.json({ error: "voicevox audio_query failed" }, mapUpstreamStatus(aqRes.status));
    }
    const query = (await aqRes.json()) as Record<string, unknown>;

    // ② 出力フォーマットを 16kHz/モノラルへ整形してから合成。
    const synRes = await fetchWithTimeout(synthesisUrl(VOICEVOX_URL, req.speaker), TIMEOUT_VOICE_MS, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(adjustAudioQuery(query)),
    });
    if (!synRes.ok) {
      console.error("voicevox synthesis failed:", synRes.status);
      return c.json({ error: "voicevox synthesis failed" }, mapUpstreamStatus(synRes.status));
    }

    // WAV バイト列をそのまま audio/wav で返しつつ、次回のためにキャッシュへ格納する。
    const wav = new Uint8Array(await synRes.arrayBuffer());
    // 上限超なら挿入順の最古を 1 件だけ追い出してから格納（件数を有界に保つ）。
    if (ttsCache.size >= TTS_CACHE_MAX) {
      const oldest = ttsCache.keys().next().value;
      if (oldest !== undefined) ttsCache.delete(oldest);
    }
    ttsCache.set(cacheKey, wav);
    return c.body(wav, 200, { "content-type": "audio/wav" });
  } catch (err) {
    console.error("voicevox call failed:", err);
    if (isTimeoutError(err)) {
      return c.json({ error: "upstream VOICEVOX timed out" }, 504);
    }
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

    const res = await fetchWithTimeout(asrUrl(STT_URL, opts), TIMEOUT_VOICE_MS, {
      method: "POST",
      body: form,
    });
    if (!res.ok) {
      console.error("whisper /asr failed:", res.status);
      return c.json({ error: "whisper /asr failed" }, mapUpstreamStatus(res.status));
    }
    // output=json なので JSON で受ける。壊れていても parseAsrText が空文字に丸める。
    const json = await res.json().catch(() => null);
    return c.json({ text: parseAsrText(json) });
  } catch (err) {
    console.error("whisper call failed:", err);
    if (isTimeoutError(err)) {
      return c.json({ error: "upstream Whisper timed out" }, 504);
    }
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
      fetchWithTimeout(pokemonUrl(POKEAPI_URL, id), TIMEOUT_STATIC_MS),
      fetchWithTimeout(speciesUrl(POKEAPI_URL, id), TIMEOUT_STATIC_MS),
    ]);
    if (!pokeRes.ok || !specRes.ok) {
      console.error("pokeapi fetch failed:", pokeRes.status, specRes.status);
      // 2 本の失敗を写像。404 だけなら 404（該当なし）、5xx 等が混じれば 502 を優先。
      return c.json(
        { error: "upstream PokeAPI fetch failed" },
        mapUpstreamStatusPair(pokeRes.status, specRes.status),
      );
    }

    const pokemon = (await pokeRes.json()) as Record<string, unknown>;
    const species = (await specRes.json()) as Record<string, unknown>;

    // ~30KB の生 JSON を実機向けコンパクト JSON へ純粋関数で削る。
    const info = extractPokemonInfo(id, pokemon, species);
    pokemonInfoCache.set(id, info);
    return c.json(info);
  } catch (err) {
    console.error("pokeapi call failed:", err);
    if (isTimeoutError(err)) {
      return c.json({ error: "upstream PokeAPI timed out" }, 504);
    }
    return c.json({ error: "upstream PokeAPI call failed" }, 502);
  }
});

// スプライト PNG の所在。PokeAPI/sprites リポジトリの GitHub raw CDN。
const SPRITE_BASE_URL =
  process.env.SPRITE_BASE_URL ??
  "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon";

// 図鑑番号 → RGB565 バイト列のオンメモリキャッシュ（1件 18432 バイト）。
// info と同じくプロセス再起動でクリア・永続化しない（fair-use / ライセンス方針）。
// キーは parsePokemonId で検証済みの id（1..MAX_POKEMON_ID=1025）に閉じるため
// 最大 1025 * 18432B ≒ 18.9MB で有界。上限を緩める時はここの有界性が崩れる点に注意。
const spriteCache = new Map<number, Uint8Array<ArrayBuffer>>();

app.get("/pokemon/sprite/:id", async (c) => {
  // 図鑑番号の検証は純粋ロジックへ委譲。不正なら 400。
  let id: number;
  try {
    id = parsePokemonId(c.req.param("id"));
  } catch (err) {
    return c.json({ error: (err as Error).message }, 400);
  }

  // キャッシュヒットなら CDN を叩かず即返す。
  const cached = spriteCache.get(id);
  if (cached) {
    return c.body(cached, 200, { "content-type": "application/octet-stream" });
  }

  try {
    const pngRes = await fetchWithTimeout(spriteUrl(SPRITE_BASE_URL, id), TIMEOUT_STATIC_MS);
    if (!pngRes.ok) {
      console.error("sprite fetch failed:", pngRes.status);
      return c.json({ error: "upstream sprite fetch failed" }, mapUpstreamStatus(pngRes.status));
    }
    const png = Buffer.from(await pngRes.arrayBuffer());

    // sharp は PNG デコード＋リサイズだけ担う（トラスト境界）。
    // 外部 CDN から取得したバイト列を初めてネイティブデコーダに通すため、多層防御として
    // 入力画素数に上限を設け（展開爆弾対策）、デコードエラーで確実に失敗させる。
    // fit:"contain" でアスペクト比を保ち、余白は透明で埋める（後段でクロマキー化される）。
    const rawRgba = await sharp(png, {
      limitInputPixels: 1024 * 1024, // スプライトは 96x96。正規の最大でも十分に余裕。
      failOn: "error",
    })
      .resize(SPRITE_SIZE, SPRITE_SIZE, {
        fit: "contain",
        background: { r: 0, g: 0, b: 0, alpha: 0 },
      })
      .ensureAlpha() // α欠落フォーマットでも4chを保証（rgbaToRgb565 の前提）。
      .raw()
      .toBuffer();

    // 純粋関数で RGB565（リトルエンディアン・透過はクロマキー）へ変換。
    const rgb565 = rgbaToRgb565(new Uint8Array(rawRgba));
    spriteCache.set(id, rgb565);
    return c.body(rgb565, 200, { "content-type": "application/octet-stream" });
  } catch (err) {
    console.error("sprite conversion failed:", err);
    if (isTimeoutError(err)) {
      return c.json({ error: "upstream sprite timed out" }, 504);
    }
    return c.json({ error: "sprite conversion failed" }, 502);
  }
});

// 鳴き声 OGG の所在。PokeAPI/cries の "latest"（全世代カバー）。
const CRY_BASE_URL =
  process.env.CRY_BASE_URL ??
  "https://raw.githubusercontent.com/PokeAPI/cries/main/cries/pokemon/latest";

// FFmpeg 実行ファイル名（PATH 解決）。環境により差し替え可能に。
const FFMPEG_BIN = process.env.FFMPEG_BIN ?? "ffmpeg";

// FFmpeg 変換のタイムアウト（ms）。無応答の子プロセスをゾンビ化させないための門番。
// 鳴き声は 1 秒程度なので 10 秒あれば十分な余裕がある。
const FFMPEG_TIMEOUT_MS = Number(process.env.FFMPEG_TIMEOUT_MS ?? 10000);

// stderr の蓄積上限。異常時に無制限連結して暴走しないよう先頭のみ保持する。
const FFMPEG_STDERR_CAP = 4096;

// 図鑑番号 → WAV バイト列のオンメモリキャッシュ（1件 ~30-64KB）。
// info/sprite と同じくプロセス再起動でクリア・永続化しない（fair-use / ライセンス方針）。
// キーは検証済み id（1..1025）に閉じ、値も MAX_CRY_BYTES 以下なので有界。
const cryCache = new Map<number, Uint8Array<ArrayBuffer>>();

// OGG バイト列を FFmpeg 子プロセスで 16kHz/mono/16bit 生 PCM へ変換する（副作用）。
// 一時ファイルを作らず stdin/stdout パイプでメモリ上完結させる。
// タイムアウト・出力サイズ上限・ゾンビ対策は runTranscode に集約（単体テスト可能）。
function transcodeOggToPcm(ogg: Uint8Array): Promise<Uint8Array> {
  return runTranscode(ogg, {
    bin: FFMPEG_BIN,
    args: ffmpegArgs(),
    // WAV ヘッダ分を除いた PCM 上限。最終 WAV が MAX_CRY_BYTES 以内に収まる。
    maxOutputBytes: MAX_CRY_BYTES - WAV_HEADER_SIZE,
    timeoutMs: FFMPEG_TIMEOUT_MS,
    stderrCap: FFMPEG_STDERR_CAP,
  });
}

app.get("/pokemon/cry/:id", async (c) => {
  // 図鑑番号の検証は純粋ロジックへ委譲。不正なら 400。
  let id: number;
  try {
    id = parsePokemonId(c.req.param("id"));
  } catch (err) {
    return c.json({ error: (err as Error).message }, 400);
  }

  // キャッシュヒットなら CDN も FFmpeg も叩かず即返す。
  const cached = cryCache.get(id);
  if (cached) {
    return c.body(cached, 200, { "content-type": "audio/wav" });
  }

  try {
    const oggRes = await fetchWithTimeout(cryUrl(CRY_BASE_URL, id), TIMEOUT_STATIC_MS);
    if (!oggRes.ok) {
      console.error("cry fetch failed:", oggRes.status);
      return c.json({ error: "upstream cry fetch failed" }, mapUpstreamStatus(oggRes.status));
    }
    const ogg = new Uint8Array(await oggRes.arrayBuffer());

    // FFmpeg で生 PCM 化 → 純粋関数で標準 WAV ヘッダを被せる（device の parse_wav_header と往復一致）。
    const pcm = await transcodeOggToPcm(ogg);
    const wav = writeWavHeader(pcm);
    cryCache.set(id, wav);
    return c.body(wav, 200, { "content-type": "audio/wav" });
  } catch (err) {
    console.error("cry conversion failed:", err);
    // 上流 OGG 取得のタイムアウトは 504。FFmpeg 変換の失敗/タイムアウトは従来通り 502 のまま。
    if (isTimeoutError(err)) {
      return c.json({ error: "upstream cry timed out" }, 504);
    }
    return c.json({ error: "cry conversion failed" }, 502);
  }
});

const port = Number(process.env.PORT ?? 3000);
serve({ fetch: app.fetch, port });
console.log(`relay listening on http://localhost:${port}`);

export default app;
