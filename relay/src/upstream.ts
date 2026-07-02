// 上流（VOICEVOX / Whisper / Claude / PokeAPI）への外向き呼び出しに共通する
// タイムアウト・エラー分類を集約する層。
// 副作用は fetchWithTimeout だけに閉じ、判定/写像は純粋関数として単体テスト可能にする。

// 用途別のタイムアウト（ms）。
// 静的 JSON/バイナリ取得は素早く詰まりを検知したいので短め、
// 入力長に処理時間が依存する準生成（音声合成/文字起こし）は中間、
// 生成に時間のかかる LLM 系は長めに取る。
export const TIMEOUT_STATIC_MS = 8_000; // PokeAPI / sprite / cry。純粋な取得。
export const TIMEOUT_VOICE_MS = 20_000; // VOICEVOX 合成 / Whisper 文字起こし。入力長依存。
export const TIMEOUT_LLM_MS = 30_000; // Claude 生成（Anthropic SDK 側にも渡す）。

// AbortSignal.timeout() の発火は DOMException(name="TimeoutError") で表れる（undici/Node）。
// AbortController.abort() 由来の "AbortError" とは名前で区別できるため、
// 「タイムアウトだけを 504 に振り分ける」判定をここに純粋関数として閉じる。
export function isTimeoutError(err: unknown): boolean {
  return err instanceof Error && err.name === "TimeoutError";
}

// 上流の失敗ステータスを中継サーバの応答ステータスへ写像する純粋関数。
// 404（該当なし）は観測性のため素通しし、それ以外の上流失敗はまとめて 502 に丸める。
export function mapUpstreamStatus(status: number): 404 | 502 {
  return status === 404 ? 404 : 502;
}

// 2 本の上流応答（並行取得）の失敗をまとめて写像する純粋関数。少なくとも一方が失敗している前提。
// 真の上流障害（404 以外の失敗）が混じるなら観測性のため 502 を優先し、
// 失敗が 404（該当なし）だけのときに限り 404 を返す。
// これにより「片方 404・片方 5xx」を 404 で覆い隠して実障害を見落とすことを防ぐ。
export function mapUpstreamStatusPair(a: number, b: number): 404 | 502 {
  const failures = [a, b].filter((s) => s >= 400);
  return failures.some((s) => s !== 404) ? 502 : 404;
}

// タイムアウト付き fetch。無応答の上流でリクエストが無期限滞留するのを防ぐ。
// 呼び出し側 init に signal を混ぜないこと（ここで上書きするため）。
export function fetchWithTimeout(
  url: string,
  timeoutMs: number,
  init?: RequestInit,
): Promise<Response> {
  return fetch(url, { ...init, signal: AbortSignal.timeout(timeoutMs) });
}
