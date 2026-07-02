import { describe, it, expect } from "vitest";
import {
  fetchWithTimeout,
  isTimeoutError,
  mapUpstreamStatus,
  mapUpstreamStatusPair,
  TIMEOUT_LLM_MS,
  TIMEOUT_STATIC_MS,
  TIMEOUT_VOICE_MS,
} from "../src/upstream";

describe("isTimeoutError", () => {
  it("AbortSignal.timeout 由来の TimeoutError を検知する", () => {
    const err = new DOMException("The operation timed out.", "TimeoutError");
    expect(isTimeoutError(err)).toBe(true);
  });
  it("手動 abort 由来の AbortError はタイムアウト扱いしない", () => {
    const err = new DOMException("aborted", "AbortError");
    expect(isTimeoutError(err)).toBe(false);
  });
  it("通常の Error / 非 Error は false", () => {
    expect(isTimeoutError(new Error("boom"))).toBe(false);
    expect(isTimeoutError("TimeoutError")).toBe(false);
    expect(isTimeoutError(null)).toBe(false);
    expect(isTimeoutError(undefined)).toBe(false);
  });
});

describe("mapUpstreamStatus", () => {
  it("上流 404 は 404 として素通しする", () => {
    expect(mapUpstreamStatus(404)).toBe(404);
  });
  it("404 以外の上流失敗は 502 に丸める", () => {
    expect(mapUpstreamStatus(500)).toBe(502);
    expect(mapUpstreamStatus(503)).toBe(502);
    expect(mapUpstreamStatus(400)).toBe(502);
    expect(mapUpstreamStatus(429)).toBe(502);
  });
});

describe("mapUpstreamStatusPair", () => {
  it("両方 404（該当なし）なら 404", () => {
    expect(mapUpstreamStatusPair(404, 404)).toBe(404);
  });
  it("片方 404・片方 200(ok) なら 404", () => {
    expect(mapUpstreamStatusPair(404, 200)).toBe(404);
    expect(mapUpstreamStatusPair(200, 404)).toBe(404);
  });
  it("片方 404・片方 5xx（真の障害）は 502 を優先して実障害を隠さない", () => {
    expect(mapUpstreamStatusPair(404, 500)).toBe(502);
    expect(mapUpstreamStatusPair(503, 404)).toBe(502);
  });
  it("両方 5xx / その他失敗なら 502", () => {
    expect(mapUpstreamStatusPair(500, 502)).toBe(502);
    expect(mapUpstreamStatusPair(429, 200)).toBe(502);
  });
});

describe("TIMEOUT 定数", () => {
  it("静的取得 < 音声（準生成）< LLM の順に長い", () => {
    expect(TIMEOUT_STATIC_MS).toBeLessThan(TIMEOUT_VOICE_MS);
    expect(TIMEOUT_VOICE_MS).toBeLessThan(TIMEOUT_LLM_MS);
  });
});

describe("fetchWithTimeout", () => {
  it("指定時間で応答が無ければ TimeoutError を投げる", async () => {
    // 解決しないサーバをフェイクして、タイムアウト発火だけを検証する。
    // 実ネットワークには出ないよう never-resolving fetch を注入する。
    const originalFetch = globalThis.fetch;
    globalThis.fetch = ((_url: string, init?: RequestInit) =>
      new Promise((_resolve, reject) => {
        // AbortSignal.timeout が発火したら fetch は abort でリジェクトする挙動を再現。
        init?.signal?.addEventListener("abort", () =>
          reject((init.signal as AbortSignal).reason),
        );
      })) as typeof fetch;
    try {
      await expect(
        fetchWithTimeout("http://example.invalid", 10),
      ).rejects.toSatisfy(isTimeoutError);
    } finally {
      globalThis.fetch = originalFetch;
    }
  });
});
