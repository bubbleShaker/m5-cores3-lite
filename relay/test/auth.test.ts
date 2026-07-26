import { describe, it, expect } from "vitest";
import { readFileSync } from "node:fs";
import {
  isPublicPath,
  loadToken,
  MIN_TOKEN_LENGTH,
  PLACEHOLDER_TOKENS,
  PUBLIC_PATHS,
  TOKEN_HEADER,
  tokenMatches,
} from "../src/auth";

// 検証を通る最小長のダミートークン。実運用では `openssl rand -hex 16` 相当を使う。
const VALID = "a".repeat(MIN_TOKEN_LENGTH);

// ⚠ process.env はまるごと差し替えない（describe 間で壊れる）。
// loadToken は env をオブジェクト引数で受けるので、テストは process.env に一切触れない。
describe("loadToken", () => {
  it("妥当なトークンをそのまま返す", () => {
    expect(loadToken({ RELAY_TOKEN: VALID })).toBe(VALID);
  });

  it("前後の空白を落とす", () => {
    expect(loadToken({ RELAY_TOKEN: `  ${VALID}\n` })).toBe(VALID);
  });

  it("未設定なら投げる（無認証で起動させないため）", () => {
    expect(() => loadToken({})).toThrow(/RELAY_TOKEN is required/);
  });

  it("空文字・空白のみでも投げる", () => {
    expect(() => loadToken({ RELAY_TOKEN: "" })).toThrow(/required/);
    expect(() => loadToken({ RELAY_TOKEN: "   " })).toThrow(/required/);
  });

  it("短すぎるトークンは投げる", () => {
    const short = "a".repeat(MIN_TOKEN_LENGTH - 1);
    expect(() => loadToken({ RELAY_TOKEN: short })).toThrow(
      new RegExp(`at least ${MIN_TOKEN_LENGTH} characters`),
    );
  });

  it("境界: ちょうど最低長は通る", () => {
    expect(loadToken({ RELAY_TOKEN: VALID })).toBe(VALID);
  });

  // テンプレートの値は public リポジトリに載っているので、長さを満たしていても弾く。
  // 「長さ検証だけ」だと編集し忘れた利用者が既知のトークンで稼働してしまう。
  it("テンプレートのプレースホルダは長さを満たしていても弾く", () => {
    for (const p of PLACEHOLDER_TOKENS) {
      expect(() => loadToken({ RELAY_TOKEN: p })).toThrow(/placeholder/);
    }
  });

  it("プレースホルダの判定は大文字小文字を区別しない", () => {
    expect(() => loadToken({ RELAY_TOKEN: "Change_Me" })).toThrow(/placeholder/);
  });

  it("テンプレートに実際に書かれている値が拒否対象に入っている", () => {
    // .env.example / secrets.h.example を書き換えたらこのテストが落ちる（同期の番人）。
    const envExample = readFileSync(
      new URL("../.env.example", import.meta.url),
      "utf8",
    );
    const headerExample = readFileSync(
      new URL("../../src/secrets.h.example", import.meta.url),
      "utf8",
    );
    const envToken = envExample.match(/^RELAY_TOKEN=(.+)$/m)?.[1].trim();
    const headerToken = headerExample.match(
      /#define RELAY_TOKEN "([^"]*)"/,
    )?.[1];

    expect(envToken).toBeTruthy();
    expect(headerToken).toBeTruthy();
    // どちらもそのままでは起動できないこと（長さ不足かプレースホルダ判定のどちらかで弾かれる）。
    expect(() => loadToken({ RELAY_TOKEN: envToken })).toThrow();
    expect(() => loadToken({ RELAY_TOKEN: headerToken })).toThrow();
  });
});

describe("tokenMatches", () => {
  it("一致すれば true", () => {
    expect(tokenMatches(VALID, VALID)).toBe(true);
  });

  it("違えば false", () => {
    expect(tokenMatches(VALID, "b".repeat(MIN_TOKEN_LENGTH))).toBe(false);
  });

  it("長さが違っても例外にせず false を返す（SHA-256 で 32 バイトに揃うため）", () => {
    expect(tokenMatches(VALID, "short")).toBe(false);
    expect(tokenMatches(VALID, VALID + "x")).toBe(false);
  });

  it("ヘッダ欠落（undefined / null / 空文字）は false", () => {
    expect(tokenMatches(VALID, undefined)).toBe(false);
    expect(tokenMatches(VALID, null)).toBe(false);
    expect(tokenMatches(VALID, "")).toBe(false);
  });

  it("前方一致では通さない（1 文字違いも拒否）", () => {
    const almost = VALID.slice(0, -1) + "b";
    expect(tokenMatches(VALID, almost)).toBe(false);
  });

  it("大文字小文字を区別する", () => {
    expect(tokenMatches(VALID, VALID.toUpperCase())).toBe(false);
  });

  it("非 ASCII でも一致・不一致を正しく判定する", () => {
    const jp = "ずんだもん".repeat(4);
    expect(tokenMatches(jp, jp)).toBe(true);
    expect(tokenMatches(jp, "ずんだもん")).toBe(false);
  });
});

describe("isPublicPath", () => {
  it("/health は免除される", () => {
    expect(isPublicPath("/health")).toBe(true);
  });

  it("末尾スラッシュの揺れを吸収する", () => {
    expect(isPublicPath("/health/")).toBe(true);
  });

  it("保護対象のパスは免除しない", () => {
    for (const p of ["/chat", "/tts", "/stt", "/pokemon/info/1", "/"]) {
      expect(isPublicPath(p)).toBe(false);
    }
  });

  it("前方一致で素通ししない（/health を接頭辞にした別パス）", () => {
    expect(isPublicPath("/healthz")).toBe(false);
    expect(isPublicPath("/health/../chat")).toBe(false);
    expect(isPublicPath("/health/secret")).toBe(false);
  });

  it("免除リストは /health だけ（増えたら意図的な変更のはず）", () => {
    expect(PUBLIC_PATHS).toEqual(["/health"]);
  });
});

describe("TOKEN_HEADER", () => {
  it("小文字で定義されている（Hono のヘッダ取得に合わせる）", () => {
    expect(TOKEN_HEADER).toBe(TOKEN_HEADER.toLowerCase());
  });
});
