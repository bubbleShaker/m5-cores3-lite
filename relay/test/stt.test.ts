import { describe, it, expect } from "vitest";
import {
  asrUrl,
  DEFAULT_LANGUAGE,
  DEFAULT_TASK,
  MAX_AUDIO_BYTES,
  parseAsrText,
  parseSttOptions,
  validateAudio,
  WAV_HEADER_SIZE,
} from "../src/stt";

// テスト用に最小の「それっぽい WAV」バイト列を作る。
// 0..3="RIFF", 8..11="WAVE" さえ満たせば validateAudio は通る。
function makeWav(size = WAV_HEADER_SIZE): Uint8Array {
  const b = new Uint8Array(size);
  b.set([0x52, 0x49, 0x46, 0x46], 0); // "RIFF"
  b.set([0x57, 0x41, 0x56, 0x45], 8); // "WAVE"
  return b;
}

describe("parseSttOptions", () => {
  it("未指定なら既定(ja/transcribe)を解決する", () => {
    expect(parseSttOptions({})).toEqual({
      language: DEFAULT_LANGUAGE,
      task: DEFAULT_TASK,
    });
  });
  it("null/undefined でも既定にフォールバックする", () => {
    expect(parseSttOptions(null)).toEqual({
      language: DEFAULT_LANGUAGE,
      task: DEFAULT_TASK,
    });
  });
  it("language を反映し前後空白を除去する", () => {
    expect(parseSttOptions({ language: "  en  " }).language).toBe("en");
  });
  it("task=translate を受理する", () => {
    expect(parseSttOptions({ task: "translate" }).task).toBe("translate");
  });
  it("空 language は弾く", () => {
    expect(() => parseSttOptions({ language: "   " })).toThrow();
    expect(() => parseSttOptions({ language: 42 })).toThrow();
  });
  it("許可外 task は弾く", () => {
    expect(() => parseSttOptions({ task: "summarize" })).toThrow();
  });
});

describe("validateAudio", () => {
  it("正しい RIFF/WAVE は通す", () => {
    expect(() => validateAudio(makeWav())).not.toThrow();
  });
  it("空バイト列は弾く", () => {
    expect(() => validateAudio(new Uint8Array(0))).toThrow();
  });
  it("ヘッダに満たない小ささは弾く", () => {
    expect(() => validateAudio(new Uint8Array(10))).toThrow();
  });
  it("RIFF/WAVE で無いものは弾く", () => {
    const notWav = new Uint8Array(WAV_HEADER_SIZE); // 全 0
    expect(() => validateAudio(notWav)).toThrow();
  });
  it("過大なサイズは弾く", () => {
    // 全長確保は重いので length だけ偽装した薄いオブジェクトで検証。
    const huge = { length: MAX_AUDIO_BYTES + 1 } as unknown as Uint8Array;
    expect(() => validateAudio(huge)).toThrow();
  });
});

describe("asrUrl", () => {
  it("encode/task/language/output をクエリに載せる", () => {
    const url = asrUrl("http://localhost:9000", {
      language: "ja",
      task: "transcribe",
    });
    expect(url).toContain("http://localhost:9000/asr?");
    expect(url).toContain("encode=true");
    expect(url).toContain("task=transcribe");
    expect(url).toContain("language=ja");
    expect(url).toContain("output=json");
  });
  it("ベース URL 末尾スラッシュを吸収する", () => {
    const url = asrUrl("http://h:9000/", { language: "ja", task: "transcribe" });
    expect(url).toContain("http://h:9000/asr?");
  });
});

describe("parseAsrText", () => {
  it("text を取り出して trim する", () => {
    expect(parseAsrText({ text: "  こんにちは  " })).toBe("こんにちは");
  });
  it("text 欠落・非文字列・壊れた入力でも空文字を返す", () => {
    expect(parseAsrText({})).toBe("");
    expect(parseAsrText({ text: 42 })).toBe("");
    expect(parseAsrText(null)).toBe("");
    expect(parseAsrText("oops")).toBe("");
  });
});
