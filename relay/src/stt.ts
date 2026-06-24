// 中継サーバ /stt の「純粋ロジック」。tts.ts と同じく fetch を持たないので vitest で完結する。
// 役割は4つ: (1) オプション検証＋既定解決 (2) 音声の検証 (3) Whisper /asr の URL 組み立て (4) レスポンス解析。
// STT エンジンは自前ホスト Whisper(docker) — onerahmet/openai-whisper-asr-webservice を想定。

// 既定の言語と処理。日本語の聞き取り（翻訳ではなく文字起こし）が基本。
export const DEFAULT_LANGUAGE = "ja";
export const DEFAULT_TASK = "transcribe";

// Whisper ASR webservice が受け付ける task はこの2種のみ。
export const ALLOWED_TASKS = ["transcribe", "translate"] as const;
export type SttTask = (typeof ALLOWED_TASKS)[number];

// 容量の下限/上限。WAV は最低でも 44 バイトのヘッダを持つ。
// 上限は数十秒の録音を想定し 10MB（PSRAM/転送量と Whisper 負荷の折衷）。
export const WAV_HEADER_SIZE = 44;
export const MAX_AUDIO_BYTES = 10 * 1024 * 1024;

export interface SttOptions {
  language: string;
  task: SttTask;
}

// /stt のオプションを検証して既定を解決する。不正なら理由付きで投げ、server.ts が 400 にする。
// 音声本体は body(raw WAV) で来るので、ここで見るのは付随オプションだけ。
export function parseSttOptions(input: unknown): SttOptions {
  const obj = (input ?? {}) as Record<string, unknown>;

  // language は任意。未指定なら既定(ja)。指定時は空でない文字列のみ受理。
  let language = DEFAULT_LANGUAGE;
  if (obj.language !== undefined && obj.language !== null) {
    if (typeof obj.language !== "string" || obj.language.trim() === "") {
      throw new Error("language must be a non-empty string");
    }
    language = obj.language.trim();
  }

  // task は任意。未指定なら既定(transcribe)。許可リスト外は弾く。
  let task: SttTask = DEFAULT_TASK;
  if (obj.task !== undefined && obj.task !== null) {
    if (!ALLOWED_TASKS.includes(obj.task as SttTask)) {
      throw new Error(`task must be one of: ${ALLOWED_TASKS.join(", ")}`);
    }
    task = obj.task as SttTask;
  }

  return { language, task };
}

// 録音バイト列が「本物の WAV」かつ妥当なサイズかを検証する。
// 壊れた/空の/過大なデータを Whisper へ投げる前に止め、502 ではなく 400 で返すための門番。
export function validateAudio(bytes: Uint8Array): void {
  if (bytes.length === 0) {
    throw new Error("audio body is empty");
  }
  if (bytes.length < WAV_HEADER_SIZE) {
    throw new Error("audio too small to be a WAV file");
  }
  if (bytes.length > MAX_AUDIO_BYTES) {
    throw new Error(`audio too large (max ${MAX_AUDIO_BYTES} bytes)`);
  }
  // RIFF/WAVE マジック: 0..3="RIFF", 8..11="WAVE"。これで WAV 以外を弾く。
  const tag = (offset: number) =>
    String.fromCharCode(
      bytes[offset],
      bytes[offset + 1],
      bytes[offset + 2],
      bytes[offset + 3],
    );
  if (tag(0) !== "RIFF" || tag(8) !== "WAVE") {
    throw new Error("audio is not a RIFF/WAVE file");
  }
}

// 末尾スラッシュの有無を吸収してベース URL を正規化する（tts.ts と同じ作法）。
function normalizeBase(baseUrl: string): string {
  return baseUrl.replace(/\/+$/, "");
}

// Whisper ASR webservice の POST /asr URL を組み立てる。
// encode=true … サーバ側で ffmpeg デコードさせる（WAV をそのまま渡せる）。
// output=json … {text, ...} の JSON で返させる。
export function asrUrl(baseUrl: string, opts: SttOptions): string {
  const params = new URLSearchParams({
    encode: "true",
    task: opts.task,
    language: opts.language,
    output: "json",
  });
  return `${normalizeBase(baseUrl)}/asr?${params.toString()}`;
}

// Whisper のレスポンス JSON から text を取り出す。
// エンジンのバージョン差や壊れた応答でも落とさず、取れなければ空文字を返す。
export function parseAsrText(json: unknown): string {
  if (json && typeof json === "object" && "text" in json) {
    const t = (json as Record<string, unknown>).text;
    if (typeof t === "string") {
      return t.trim();
    }
  }
  return "";
}
