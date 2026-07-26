// 環境変数の解釈のテスト（#219）。
// ここが壊れると「声が届いた瞬間に PC が動く」「全操作が即タイムアウトする」といった、
// **他のテストでは気づけない**壊れ方をするので、表で固定する。
import { describe, it, expect } from "vitest";
import { execTimeoutMs, isDryRun } from "../src/config";
import { DEFAULT_TIMEOUT_MS } from "../src/executor";

describe("isDryRun（既定は dry-run）", () => {
  it("未設定なら dry-run", () => {
    expect(isDryRun({})).toBe(true);
    expect(isDryRun({ RELAY_DRY_RUN: undefined })).toBe(true);
  });

  it("0 以外は全て dry-run（実行は明示的な opt-in のみ）", () => {
    for (const value of ["1", "", "true", "false", "no", "yes", "00", " 0", "O"]) {
      expect(isDryRun({ RELAY_DRY_RUN: value })).toBe(true);
    }
  });

  it("0 のときだけ実行する", () => {
    expect(isDryRun({ RELAY_DRY_RUN: "0" })).toBe(false);
  });
});

describe("execTimeoutMs", () => {
  it("未設定・空文字は既定値（不正扱いにしない）", () => {
    expect(execTimeoutMs({})).toEqual({ value: DEFAULT_TIMEOUT_MS, invalid: false });
    expect(execTimeoutMs({ RELAY_EXEC_TIMEOUT_MS: "  " })).toEqual({
      value: DEFAULT_TIMEOUT_MS,
      invalid: false,
    });
  });

  it("妥当な値はそのまま使う", () => {
    expect(execTimeoutMs({ RELAY_EXEC_TIMEOUT_MS: "5000" }).value).toBe(5000);
  });

  it("不正値は既定値へ落とす（全て「即タイムアウト」という同じ症状を招くため）", () => {
    // 0 / 負数 / 非数値 / 32bit 超（setTimeout が 1ms に丸める）。
    for (const value of ["0", "-1", "abc", "NaN", "Infinity", "3000000000"]) {
      expect(execTimeoutMs({ RELAY_EXEC_TIMEOUT_MS: value })).toEqual({
        value: DEFAULT_TIMEOUT_MS,
        invalid: true,
      });
    }
  });
});
