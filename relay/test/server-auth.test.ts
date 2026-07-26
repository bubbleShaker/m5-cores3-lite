// 認証の「結線」のテスト（#216）。auth.ts の単体テストとは別物で、こちらが本当の境界。
//
// 純粋ロジックが正しくても、`app.use("*")` をルート定義より後に登録した／免除の範囲を
// 間違えた、といった配線ミスは検出できない。ここでは server.ts が組み立てた実物の app を
// 取り込み、app.request() で HTTP レベルの挙動を確かめる。
//
// 上流（Claude / VOICEVOX / Whisper / PokeAPI）は一切呼ばない。認証を通す確認は
// 「正しいトークン + 壊れた body」で行い、body 検証の 400 で止まるようにしてある。
import { describe, it, expect, beforeAll, afterAll, vi } from "vitest";
import { TOKEN_HEADER } from "../src/auth";

const TOKEN = "integration-token-0123456789ab";
const JSON_HEADERS = { "content-type": "application/json" };

// ⚠ process.env はまるごと差し替えない（describe 間で壊れる）。個別キーだけ退避・復元する。
let prevToken: string | undefined;
let prevApiKey: string | undefined;
// Hono の request() は Response も Promise<Response> も返し得る（await でどちらも扱える）。
let app: {
  request: (path: string, init?: RequestInit) => Response | Promise<Response>;
};

beforeAll(async () => {
  prevToken = process.env.RELAY_TOKEN;
  prevApiKey = process.env.ANTHROPIC_API_KEY;
  // server.ts の process.loadEnvFile() は既存の環境変数を上書きしないので、
  // ここで入れた値が relay/.env より優先される（実測で確認済み）。
  process.env.RELAY_TOKEN = TOKEN;
  process.env.ANTHROPIC_API_KEY = "dummy-not-used";
  // 401 のたびに console.warn が出てテスト出力が汚れるので黙らせる。
  vi.spyOn(console, "warn").mockImplementation(() => {});
  app = (await import("../src/server")).default;
});

afterAll(() => {
  if (prevToken === undefined) delete process.env.RELAY_TOKEN;
  else process.env.RELAY_TOKEN = prevToken;
  if (prevApiKey === undefined) delete process.env.ANTHROPIC_API_KEY;
  else process.env.ANTHROPIC_API_KEY = prevApiKey;
  vi.restoreAllMocks();
});

// 認証が要る全ルート。**新しいエンドポイントを足したらここにも足す。**
const PROTECTED: ReadonlyArray<[string, RequestInit]> = [
  ["/chat", { method: "POST", headers: JSON_HEADERS, body: '{"message":"hi"}' }],
  ["/tts", { method: "POST", headers: JSON_HEADERS, body: '{"text":"あ"}' }],
  ["/stt", { method: "POST", body: "x" }],
  ["/pokemon/info/1", { method: "GET" }],
  ["/pokemon/sprite/1", { method: "GET" }],
  ["/pokemon/cry/1", { method: "GET" }],
];

describe("認証ミドルウェアの結線", () => {
  it("/health はトークン無しで通る", async () => {
    const res = await app.request("/health");
    expect(res.status).toBe(200);
    expect(await res.json()).toEqual({ ok: true });
  });

  it.each(PROTECTED)("%s はトークン無しなら 401", async (path, init) => {
    const res = await app.request(path, init);
    expect(res.status).toBe(401);
    expect(await res.json()).toEqual({ error: "unauthorized" });
  });

  it.each(PROTECTED)("%s は誤ったトークンなら 401", async (path, init) => {
    const res = await app.request(path, {
      ...init,
      headers: { ...(init.headers as Record<string, string>), [TOKEN_HEADER]: "wrong-token-abcdefghij" },
    });
    expect(res.status).toBe(401);
  });

  it("1 文字違い・1 文字多いトークンも 401", async () => {
    for (const bad of [TOKEN + "x", TOKEN.slice(0, -1) + "z", TOKEN.toUpperCase()]) {
      const res = await app.request("/pokemon/info/1", {
        headers: { [TOKEN_HEADER]: bad },
      });
      expect(res.status).toBe(401);
    }
  });

  it("未定義のパスも認証を要求する（404 より先に 401）", async () => {
    const res = await app.request("/no-such-endpoint", {
      method: "POST",
      body: "x",
    });
    expect(res.status).toBe(401);
  });

  it("/health を接頭辞にしたパスは免除されない", async () => {
    for (const path of ["/healthz", "/health/secret", "/health/../chat"]) {
      const res = await app.request(path);
      expect(res.status).toBe(401);
    }
  });

  it("正しいトークンなら認証を通過し、body 検証まで到達する（上流は呼ばない）", async () => {
    // 400 = 「認証は通り、その先の入力検証で弾かれた」証拠。401 のままなら結線が壊れている。
    const res = await app.request("/chat", {
      method: "POST",
      headers: { ...JSON_HEADERS, [TOKEN_HEADER]: TOKEN },
      body: "{}",
    });
    expect(res.status).toBe(400);
  });

  it("ヘッダ名は大文字小文字を区別しない（実機は X-Relay-Token で送る）", async () => {
    const res = await app.request("/tts", {
      method: "POST",
      headers: { ...JSON_HEADERS, "X-Relay-Token": TOKEN },
      body: "{}",
    });
    expect(res.status).toBe(400);
  });
});
