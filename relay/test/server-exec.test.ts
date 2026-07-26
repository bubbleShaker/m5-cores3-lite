// /chat と実行アダプタの「結線」のテスト（#219）。
//
// executor.test.ts は実行層単体を、これは **RELAY_DRY_RUN=0 の時に実際に呼ばれるか**を見る。
// 結線が切れていると「操作したつもりで何も起きない」になるが、純粋ロジックのテストでは気づけない。
// executor はモックするので、このテストでも**アプリは 1 つも起動しない**。
import { describe, it, expect, beforeAll, afterAll, vi } from "vitest";
import { writeFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { TOKEN_HEADER } from "../src/auth";
import type { ToolCall } from "../src/tools";
import type { ExecResult } from "../src/executor";

const TOKEN = "integration-token-0123456789ab";
const APPS_FILE = join(tmpdir(), `relay-apps-exec-${process.pid}.json`);

// vi.mock はファイル先頭へ巻き上げられるので、共有状態は vi.hoisted で先に作る。
const mocks = vi.hoisted(() => ({
  executed: [] as ToolCall[],
  result: { outcome: "ok", detail: "" } as ExecResult,
}));

// createExecutor だけ差し替え、他（isDryRun 等）は本物のまま使う。
vi.mock("../src/executor", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../src/executor")>();
  return {
    ...actual,
    createExecutor: () => ({
      execute: async (call: ToolCall) => {
        mocks.executed.push(call);
        return mocks.result;
      },
    }),
  };
});

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
let logs: string[] = [];
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
    JSON.stringify({ browser: "C:/x/msedge.exe", editor: "C:/y/Code.exe" }),
  );
  save("RELAY_TOKEN", TOKEN);
  save("ANTHROPIC_API_KEY", "dummy-not-used");
  save("RELAY_APPS_FILE", APPS_FILE);
  save("RELAY_DRY_RUN", "0"); // ← 実行を有効化（既定は dry-run）
  vi.spyOn(console, "warn").mockImplementation(() => {});
  vi.spyOn(console, "log").mockImplementation((...args: unknown[]) => {
    logs.push(args.map(String).join(" "));
  });
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

async function chat(text: string, message = "テスト") {
  claudeText = text;
  mocks.executed.length = 0;
  logs = [];
  const res = await app.request("/chat", {
    method: "POST",
    headers: { "content-type": "application/json", [TOKEN_HEADER]: TOKEN },
    body: JSON.stringify({ message }),
  });
  return { status: res.status, body: (await res.json()) as Record<string, unknown> };
}

const auditLines = () =>
  logs.filter((l) => l.startsWith("audit ")).map((l) => JSON.parse(l.slice(6)));
// 実行経路では「実行前（start）」と「結果」の 2 行が出る。結果の方を見たい時に使う。
const auditResult = () => auditLines().filter((r) => r.outcome !== "start").at(-1);

describe("/chat の実行結線（RELAY_DRY_RUN=0）", () => {
  it("検証済みの操作が実行層へ渡る", async () => {
    const { status, body } = await chat(
      '{"reply":"開くね","expression":"happy","action":"none","tool":{"name":"launch_app","args":{"app":"browser"}}}',
    );
    expect(status).toBe(200);
    expect(mocks.executed).toEqual([{ name: "launch_app", app: "browser" }]);
    // 返答は LLM の文ではなく、実際に起きたことを表す定型文になる。
    expect(body.reply).toBe("browser を開いたのだ。");
  });

  it("拒否された操作は実行層へ届かない", async () => {
    const { body } = await chat(
      '{"reply":"開くね","expression":"happy","action":"none","tool":{"name":"launch_app","args":{"app":"cmd"}}}',
    );
    expect(mocks.executed).toEqual([]);
    expect(body.reply).toContain("できない");
  });

  it("未知のツールも実行層へ届かない", async () => {
    await chat(
      '{"reply":"やるね","expression":"happy","action":"none","tool":{"name":"run_shell","args":{"cmd":"del C:/"}}}',
    );
    expect(mocks.executed).toEqual([]);
  });

  it("操作を伴わない会話では実行層を呼ばない", async () => {
    const { body } = await chat(
      '{"reply":"こんにちはなのだ","expression":"happy","action":"none","tool":{"name":"reply_only"}}',
    );
    expect(mocks.executed).toEqual([]);
    expect(body.reply).toBe("こんにちはなのだ");
  });

  it("実行に失敗しても 200 で日本語の返答になる（例外で落ちない）", async () => {
    mocks.result = { outcome: "failed", detail: "spawn ENOENT" };
    const { status, body } = await chat(
      '{"reply":"開いたよ！","expression":"happy","action":"none","tool":{"name":"launch_app","args":{"app":"browser"}}}',
    );
    expect(status).toBe(200);
    expect(body.reply).not.toContain("開いたよ");
    expect(body.reply).toBe("うまくいかなかったのだ。");
    mocks.result = { outcome: "ok", detail: "" };
  });

  it("窓が無い場合は失敗と別の言い方をする", async () => {
    mocks.result = { outcome: "not_running", detail: "窓が見つからない" };
    const { body } = await chat(
      '{"reply":"最小化するね","expression":"happy","action":"none","tool":{"name":"window_state","args":{"app":"browser","state":"minimize"}}}',
    );
    expect(body.reply).toBe("まだ開いていないのだ。");
    mocks.result = { outcome: "ok", detail: "" };
  });

  it("監査ログに発話・ツール・引数・結果が残る", async () => {
    await chat(
      '{"reply":"最小化するね","expression":"happy","action":"none","tool":{"name":"window_state","args":{"app":"browser","state":"minimize"}}}',
      "ブラウザ最小化して",
    );
    const record = auditResult();
    expect(record.utterance).toBe("ブラウザ最小化して");
    expect(record.tool).toBe("window_state");
    expect(record.app).toBe("browser");
    expect(record.state).toBe("minimize");
    expect(record.outcome).toBe("ok");
    expect(typeof record.time).toBe("string");
  });

  it("実行の前にも 1 行残す（副作用の直後に落ちても記録が消えない）", async () => {
    await chat(
      '{"reply":"開くね","expression":"happy","action":"none","tool":{"name":"launch_app","args":{"app":"browser"}}}',
      "ブラウザ開いて",
    );
    const records = auditLines();
    expect(records.map((r) => r.outcome)).toEqual(["start", "ok"]);
    expect(records[0].tool).toBe("launch_app");
    expect(records[0].app).toBe("browser");
  });

  it("拒否も監査ログに残り、要求された内容が分かる", async () => {
    await chat(
      '{"reply":"開くね","expression":"happy","action":"none","tool":{"name":"launch_app","args":{"app":"cmd"}}}',
      "コマンドプロンプト開いて",
    );
    const record = auditResult();
    expect(record.outcome).toBe("denied");
    expect(record.utterance).toBe("コマンドプロンプト開いて");
    // ⚠ 拒否すると tool は reply_only に落ちる。要求内容が残っていないと
    // 「何をやろうとして止められたのか」が監査ログから消える。
    expect(record.requested_tool).toBe("launch_app");
    expect(record.requested_app).toBe("cmd");
    // 拒否は実行前で止まるので start 行は出ない。
    expect(auditLines().map((r) => r.outcome)).toEqual(["denied"]);
  });

  it("未知のツールも要求内容が監査ログに残る", async () => {
    await chat(
      '{"reply":"やるね","expression":"happy","action":"none","tool":{"name":"run_shell","args":{"cmd":"del C:/"}}}',
      "全部消して",
    );
    const record = auditResult();
    expect(record.outcome).toBe("denied");
    expect(record.requested_tool).toBe("run_shell");
  });
});
