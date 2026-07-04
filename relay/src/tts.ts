// 中継サーバ /tts の「純粋ロジック」。chat.ts と同じく fetch を持たないので vitest で完結する。
// 役割は3つ: (1) 入力検証＋話者解決 (2) VOICEVOX audio_query の整形 (3) VOICEVOX URL 組み立て。

// VOICEVOX の既定話者。3 = ずんだもん（ノーマル）。
export const DEFAULT_SPEAKER = 3;

// デバイス互換のための出力フォーマット（#107）。
// VOICEVOX ネイティブの 24kHz をそのまま使う。以前は 16kHz へダウンサンプルしていたが、
// 高域が落ちてこもるため、クリアさ優先で 24kHz に上げる（M5 側は WAV ヘッダのレートで playRaw）。
// 代償として同じ文字数でも WAV が約1.5倍になるので、実機側は受信上限を引き上げてある。
export const OUTPUT_SAMPLING_RATE = 24000;
export const OUTPUT_STEREO = false;

// 抑揚(intonation)の強さ。1.0 = VOICEVOX 素のまま。わずかに上げて機械的な平板さを減らし、
// 「もう少し自然なイントネーション」にする。上げ過ぎると誇張され不自然になるので控えめに。
export const INTONATION_SCALE = 1.1;

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

// VOICEVOX /audio_query が返す JSON は多数のフィールドを持つが、ここでは出力フォーマットと
// 抑揚(intonationScale)だけ差し替える。アクセント句(accent_phrases)など他の韻律は保持したいので、
// スプレッドで元を残しつつ必要なキーだけ上書きする。
export function adjustAudioQuery(
  query: Record<string, unknown>,
): Record<string, unknown> {
  return {
    ...query,
    intonationScale: INTONATION_SCALE,  // 抑揚を控えめに強めて平板さを減らす（#107）
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

// 合成 WAV キャッシュのキー（#98）。同じ (speaker, text) は同じ音声になるので使い回せる。
// text は任意文字列だが、speaker は非負整数（数字のみ）で ":" を含まない。最初の ":" で speaker と
// text を一意に分離できるので、text 内に ":" があってもキーは衝突しない。
export function ttsCacheKey(text: string, speaker: number): string {
  return `${speaker}:${text}`;
}
