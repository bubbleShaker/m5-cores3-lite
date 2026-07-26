// /chat とツール検証の「結線」のテスト（#218）。
//
// tools.test.ts は純粋ロジックを厚く検証しているが、それでは守れない穴がある:
//   parseClaudeReply(text, APPS.keys) の **第 2 引数を落としても型エラーにならない**
//   （既定値 [] があるため）。落とすと「許可アプリが常に空」になり操作が全部拒否される、
//   あるいは将来の書き換え次第で許可リストを迂回する。ここはテストでしか守れない。
//
// Claude SDK はモックする。上流は一切呼ばない（課金ゼロ・ネットワーク不要）。
import { describe, it, expect, beforeAll, afterAll, vi } from "vitest";
import { writeFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { TOKEN_HEADER } from "../src/auth";

const TOKEN = "integration-token-0123456789ab";
const APPS_FILE = join(tmpdir(), `relay-apps-${process.pid}.json`);

// Claude が返してくるテキストをテストごとに差し替える。
let claudeText = "";

vi.mock("@anthropic-ai/sdk", () => {
  class APIConnectionTimeoutError extends Error {}
  class MockAnthropic {
    static APIConnectionTimeoutError = APIConnectionTimeoutError;
    messages = {
      create: async () => ({ content: [{ type: "text", text: claudeText }] }),
    };
  }
  return {
    default: Object.assign(MockAnthropic, { APIConnectionTimeoutError }),
    APIConnectionTimeoutError,
  };
});

let prev: Record<string, string | undefined> = {};
let app: {
  request: (path: string, init?: RequestInit) => Response | Promise<Response>;
};

const save = (key: string, value: string) => {
  prev[key] = process.env[key];
  process.env[key] = value;
};

beforeAll(async () => {
  writeFileSync(
    APPS_FILE,
    JSON.stringify({ browser: "C:/x/msedge.exe", editor: "C:/y/code.exe" }),
  );
  save("RELAY_TOKEN", TOKEN);
  save("ANTHROPIC_API_KEY", "dummy-not-used");
  save("RELAY_APPS_FILE", APPS_FILE);
  save("RELAY_DRY_RUN", "1");
  vi.spyOn(console, "warn").mockImplementation(() => {});
  vi.spyOn(console, "log").mockImplementation(() => {});
  app = (await import("../src/server")).default;
});

afterAll(() => {
  for (const [key, value] of Object.entries(prev)) {
    if (value === undefined) delete process.env[key];
    else process.env[key] = value;
  }
  prev = {};
  rmSync(APPS_FILE, { force: true });
  vi.restoreAllMocks();
});

async function chat(text: string) {
  claudeText = text;
  const res = await app.request("/chat", {
    method: "POST",
    headers: { "content-type": "application/json", [TOKEN_HEADER]: TOKEN },
    body: JSON.stringify({ message: "テスト" }),
  });
  return { status: res.status, body: (await res.json()) as Record<string, unknown> };
}

describe("/chat のツール結線", () => {
  it("apps.json のキーが検証に効いている（許可リストが配線されている）", async () => {
    const { status, body } = await chat(
      '{"reply":"開くね","expression":"happy","action":"none","tool":{"name":"launch_app","args":{"app":"browser"}}}',
    );
    expect(status).toBe(200);
    // ここが reply_only になるなら APPS.keys が渡っていない（＝結線が切れている）。
    expect(body.tool).toEqual({ name: "launch_app", app: "browser" });
  });

  it("apps.json に無いアプリは拒否される", async () => {
    const { body } = await chat(
      '{"reply":"開くね","expression":"happy","action":"none","tool":{"name":"launch_app","args":{"app":"cmd"}}}',
    );
    expect(body.tool).toEqual({ name: "reply_only" });
  });

  it("未知のツールは拒否される", async () => {
    const { body } = await chat(
      '{"reply":"やるね","expression":"happy","action":"none","tool":{"name":"run_shell","args":{"cmd":"rm -rf /"}}}',
    );
    expect(body.tool).toEqual({ name: "reply_only" });
  });

  it("拒否時は成功したと喋らない（LLM の reply を差し替える）", async () => {
    const { body } = await chat(
      '{"reply":"開いたよ！","expression":"happy","action":"none","tool":{"name":"launch_app","args":{"app":"cmd"}}}',
    );
    expect(body.reply).not.toContain("開いたよ");
    expect(body.reply).toContain("できない");
  });

  it("dry-run 中は「やった」と喋らない（実行層が無いので嘘になる）", async () => {
    const { body } = await chat(
      '{"reply":"開いたよ！","expression":"happy","action":"none","tool":{"name":"launch_app","args":{"app":"browser"}}}',
    );
    expect(body.reply).not.toContain("開いたよ");
    expect(body.reply).toContain("まだ");
  });

  it("操作を伴わない会話は LLM の返答をそのまま返す", async () => {
    const { body } = await chat(
      '{"reply":"こんにちはなのだ","expression":"happy","action":"none","tool":{"name":"reply_only"}}',
    );
    expect(body.reply).toBe("こんにちはなのだ");
    expect(body.tool).toEqual({ name: "reply_only" });
  });

  it("内部用の rejectedTool をレスポンスへ漏らさない", async () => {
    const { body } = await chat(
      '{"reply":"x","expression":"happy","action":"none","tool":{"name":"launch_app","args":{"app":"cmd"}}}',
    );
    expect(body).not.toHaveProperty("rejectedTool");
    expect(Object.keys(body).sort()).toEqual(["action", "expression", "reply", "tool"]);
  });

  it("既存の契約（reply/expression/action）は保たれている", async () => {
    const { body } = await chat(
      '{"reply":"やあ","expression":"thinking","action":"notify"}',
    );
    expect(body.reply).toBe("やあ");
    expect(body.expression).toBe("thinking");
    expect(body.action).toBe("notify");
  });
});
