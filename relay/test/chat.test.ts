import { describe, it, expect } from "vitest";
import {
  buildMessages,
  buildSystemPrompt,
  normalizeAction,
  normalizeExpression,
  parseClaudeReply,
  SYSTEM_PROMPT,
} from "../src/chat";

describe("normalizeExpression", () => {
  it("有効な語彙はそのまま通す", () => {
    expect(normalizeExpression("happy")).toBe("happy");
    expect(normalizeExpression("thinking")).toBe("thinking");
  });
  it("大文字・前後空白は正規化する", () => {
    expect(normalizeExpression("  HAPPY ")).toBe("happy");
  });
  it("語彙外・非文字列は neutral に倒す", () => {
    expect(normalizeExpression("excited")).toBe("neutral");
    expect(normalizeExpression(undefined)).toBe("neutral");
    expect(normalizeExpression(42)).toBe("neutral");
  });
});

describe("normalizeAction", () => {
  it("有効な action はそのまま通す", () => {
    expect(normalizeAction("notify")).toBe("notify");
    expect(normalizeAction("none")).toBe("none");
  });
  it("語彙外は none に倒す", () => {
    expect(normalizeAction("alert")).toBe("none");
    expect(normalizeAction(null)).toBe("none");
  });
});

describe("buildMessages", () => {
  it("ユーザー発話を1件の user message にする", () => {
    expect(buildMessages("こんにちは")).toEqual([
      { role: "user", content: "こんにちは" },
    ]);
  });
});

describe("SYSTEM_PROMPT", () => {
  it("JSON 出力を指示している", () => {
    expect(SYSTEM_PROMPT).toContain("JSON");
    expect(SYSTEM_PROMPT).toContain("expression");
  });
});

// 操作を伴わない応答の既定値（#218 で tool / rejectedTool が加わった）。
const NO_TOOL = { tool: { name: "reply_only" }, rejectedTool: null };

describe("parseClaudeReply", () => {
  it("正しい JSON をそのままパースする", () => {
    const out = parseClaudeReply(
      '{"reply":"やあ","expression":"happy","action":"none"}',
    );
    expect(out).toEqual({ reply: "やあ", expression: "happy", action: "none", ...NO_TOOL });
  });
  it("前後に文が付いていても JSON を拾う", () => {
    const out = parseClaudeReply(
      'はい、これです -> {"reply":"考え中","expression":"thinking","action":"notify"} 以上',
    );
    expect(out).toEqual({
      reply: "考え中",
      expression: "thinking",
      action: "notify",
      ...NO_TOOL,
    });
  });
  it("不正な expression は neutral にフォールバックする", () => {
    const out = parseClaudeReply(
      '{"reply":"x","expression":"angry","action":"none"}',
    );
    expect(out.expression).toBe("neutral");
  });
  it("JSON にできないテキストは全体を reply 扱いにする", () => {
    const out = parseClaudeReply("ただのテキスト");
    expect(out).toEqual({
      reply: "ただのテキスト",
      expression: "neutral",
      action: "none",
      ...NO_TOOL,
    });
  });
  it("reply 欠落時は空文字にする", () => {
    const out = parseClaudeReply('{"expression":"sad"}');
    expect(out).toEqual({ reply: "", expression: "sad", action: "none", ...NO_TOOL });
  });
});

// #218: tool フィールドの取り込み。検証そのものは tools.test.ts に厚く書いてあるので、
// ここでは「chat.ts が tools.ts へ正しく委譲しているか」だけを見る。
describe("parseClaudeReply — tool の取り込み", () => {
  const APPS = ["browser", "editor"];

  it("許可されたアプリの操作を受け取る", () => {
    const out = parseClaudeReply(
      '{"reply":"開くね","expression":"happy","action":"none","tool":{"name":"launch_app","args":{"app":"browser"}}}',
      APPS,
    );
    expect(out.tool).toEqual({ name: "launch_app", app: "browser" });
    expect(out.rejectedTool).toBeNull();
    expect(out.reply).toBe("開くね");
  });

  it("許可リストを渡さなければ操作は一切通らない（安全側の既定）", () => {
    const out = parseClaudeReply(
      '{"reply":"開くね","expression":"happy","action":"none","tool":{"name":"launch_app","args":{"app":"browser"}}}',
    );
    expect(out.tool).toEqual({ name: "reply_only" });
    expect(out.rejectedTool).toBeTruthy();
  });

  it("未知のツールを要求されても reply_only に落ち、理由が残る", () => {
    const out = parseClaudeReply(
      '{"reply":"やるね","expression":"happy","action":"none","tool":{"name":"run_shell","args":{"cmd":"rm -rf /"}}}',
      APPS,
    );
    expect(out.tool).toEqual({ name: "reply_only" });
    expect(out.rejectedTool).toMatch(/未知のツール/);
  });

  it("JSON として壊れている応答では操作しない（意図を推測して PC を触らない）", () => {
    const out = parseClaudeReply("ブラウザを開きます launch_app browser", APPS);
    expect(out.tool).toEqual({ name: "reply_only" });
  });
});

describe("buildSystemPrompt", () => {
  it("既存の指示を保ったままツールの説明を足す", () => {
    const p = buildSystemPrompt(["browser"]);
    expect(p).toContain(SYSTEM_PROMPT);
    expect(p).toContain("tool");
    expect(p).toContain("browser");
    expect(p).toContain("launch_app");
  });

  it("許可アプリが無ければ操作の選択肢を見せない", () => {
    const p = buildSystemPrompt([]);
    expect(p).toContain(SYSTEM_PROMPT);
    expect(p).not.toContain("launch_app");
  });
});
