// 監査ログ整形のテスト（#219）。
import { describe, it, expect } from "vitest";
import { formatAudit } from "../src/audit";

const TIME = "2026-07-26T09:00:00.000Z";

describe("formatAudit", () => {
  it("いつ・どの発話から・どのツールを・どの引数で・どうなったかを残す", () => {
    const line = formatAudit({
      time: TIME,
      utterance: "ブラウザ開いて",
      call: { name: "launch_app", app: "browser" },
      outcome: "ok",
    });
    expect(line.startsWith("audit ")).toBe(true);
    const record = JSON.parse(line.slice("audit ".length));
    expect(record).toEqual({
      time: TIME,
      utterance: "ブラウザ開いて",
      tool: "launch_app",
      app: "browser",
      outcome: "ok",
    });
  });

  it("window_state は state も残す", () => {
    const record = JSON.parse(
      formatAudit({
        time: TIME,
        utterance: "最小化して",
        call: { name: "window_state", app: "browser", state: "minimize" },
        outcome: "ok",
      }).slice("audit ".length),
    );
    expect(record.state).toBe("minimize");
  });

  it("reply_only では app を出さない", () => {
    const record = JSON.parse(
      formatAudit({
        time: TIME,
        utterance: "こんにちは",
        call: { name: "reply_only" },
        outcome: "denied",
        detail: "未知のツール: run_shell",
      }).slice("audit ".length),
    );
    expect(record).not.toHaveProperty("app");
    expect(record.detail).toBe("未知のツール: run_shell");
  });

  it("改行を含む発話でも 1 行に収まる（ログ行の偽装を防ぐ）", () => {
    const line = formatAudit({
      time: TIME,
      utterance: 'あ\naudit {"tool":"launch_app","outcome":"ok"}',
      call: { name: "reply_only" },
      outcome: "dry-run",
    });
    expect(line.split("\n")).toHaveLength(1);
    // JSON へ埋め込む際にエスケープされるので、生の改行は残らない。
    expect(JSON.parse(line.slice("audit ".length)).utterance).not.toContain("\n");
  });

  it("長すぎる発話は切り詰める", () => {
    const record = JSON.parse(
      formatAudit({
        time: TIME,
        utterance: "あ".repeat(500),
        call: { name: "reply_only" },
        outcome: "ok",
      }).slice("audit ".length),
    );
    expect([...record.utterance].length).toBeLessThanOrEqual(121);
  });
});
