// 中継サーバ /tts の「純粋ロジック」。chat.ts と同じく fetch を持たないので vitest で完結する。
// 役割は3つ: (1) 入力検証＋話者解決 (2) VOICEVOX audio_query の整形 (3) VOICEVOX URL 組み立て。

// VOICEVOX の既定話者。3 = ずんだもん（ノーマル）。
export const DEFAULT_SPEAKER = 3;

// デバイス互換のための出力フォーマット。
// M5 側で playRaw するため 16kHz / モノラルへ落とす（容量も小さくなる）。
export const OUTPUT_SAMPLING_RATE = 16000;
export const OUTPUT_STEREO = false;

// reply が長すぎると WAV が肥大化（<=500KB 目安）するので上限を設ける。
export const MAX_TEXT_LENGTH = 200;

export interface TtsRequest {
  text: string;
  speaker: number;
}

// /tts の入力を検証して話者を解決する。不正なら理由付きで投げ、server.ts が 400 にする。
export function parseTtsRequest(body: unknown): TtsRequest {
  const obj = (body ?? {}) as Record<string, unknown>;

  if (typeof obj.text !== "string" || obj.text.trim() === "") {
    throw new Error("text (non-empty string) is required");
  }
  const text = obj.text.trim();
  if (text.length > MAX_TEXT_LENGTH) {
    throw new Error(`text too long (max ${MAX_TEXT_LENGTH} chars)`);
  }

  // voice_id は任意。未指定なら既定話者。数値化できなければ不正扱い。
  let speaker = DEFAULT_SPEAKER;
  if (obj.voice_id !== undefined && obj.voice_id !== null) {
    const n =
      typeof obj.voice_id === "number" ? obj.voice_id : Number(obj.voice_id);
    if (!Number.isInteger(n) || n < 0) {
      throw new Error("voice_id must be a non-negative integer");
    }
    speaker = n;
  }

  return { text, speaker };
}

// VOICEVOX /audio_query が返す JSON は多数のフィールドを持つが、ここでは出力フォーマットだけ差し替える。
// 元の韻律パラメータ（アクセント等）は保持したいので、スプレッドで上書きする。
export function adjustAudioQuery(
  query: Record<string, unknown>,
): Record<string, unknown> {
  return {
    ...query,
    outputSamplingRate: OUTPUT_SAMPLING_RATE,
    outputStereo: OUTPUT_STEREO,
  };
}

// 末尾スラッシュの有無を吸収してベース URL を正規化する。
function normalizeBase(baseUrl: string): string {
  return baseUrl.replace(/\/+$/, "");
}

// POST /audio_query?text=...&speaker=... の URL を組み立てる。
export function audioQueryUrl(
  baseUrl: string,
  text: string,
  speaker: number,
): string {
  const params = new URLSearchParams({ text, speaker: String(speaker) });
  return `${normalizeBase(baseUrl)}/audio_query?${params.toString()}`;
}

// POST /synthesis?speaker=... の URL を組み立てる（body に audio_query JSON を載せる）。
export function synthesisUrl(baseUrl: string, speaker: number): string {
  const params = new URLSearchParams({ speaker: String(speaker) });
  return `${normalizeBase(baseUrl)}/synthesis?${params.toString()}`;
}
