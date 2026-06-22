import { describe, it, expect } from "vitest";
import {
  buildMessages,
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

describe("parseClaudeReply", () => {
  it("正しい JSON をそのままパースする", () => {
    const out = parseClaudeReply(
      '{"reply":"やあ","expression":"happy","action":"none"}',
    );
    expect(out).toEqual({ reply: "やあ", expression: "happy", action: "none" });
  });
  it("前後に文が付いていても JSON を拾う", () => {
    const out = parseClaudeReply(
      'はい、これです -> {"reply":"考え中","expression":"thinking","action":"notify"} 以上',
    );
    expect(out).toEqual({
      reply: "考え中",
      expression: "thinking",
      action: "notify",
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
    });
  });
  it("reply 欠落時は空文字にする", () => {
    const out = parseClaudeReply('{"expression":"sad"}');
    expect(out).toEqual({ reply: "", expression: "sad", action: "none" });
  });
});
