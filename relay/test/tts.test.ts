import { describe, it, expect } from "vitest";
import {
  adjustAudioQuery,
  audioQueryUrl,
  DEFAULT_SPEAKER,
  MAX_TEXT_LENGTH,
  OUTPUT_SAMPLING_RATE,
  OUTPUT_STEREO,
  parseTtsRequest,
  synthesisUrl,
} from "../src/tts";

describe("parseTtsRequest", () => {
  it("text のみなら既定話者を解決する", () => {
    expect(parseTtsRequest({ text: "こんにちは" })).toEqual({
      text: "こんにちは",
      speaker: DEFAULT_SPEAKER,
    });
  });
  it("前後空白を除去する", () => {
    expect(parseTtsRequest({ text: "  やあ  " }).text).toBe("やあ");
  });
  it("voice_id（数値）を話者に反映する", () => {
    expect(parseTtsRequest({ text: "a", voice_id: 1 }).speaker).toBe(1);
  });
  it("voice_id（数値文字列）も受理する", () => {
    expect(parseTtsRequest({ text: "a", voice_id: "8" }).speaker).toBe(8);
  });
  it("text 欠落・空文字・非文字列は弾く", () => {
    expect(() => parseTtsRequest({})).toThrow();
    expect(() => parseTtsRequest({ text: "   " })).toThrow();
    expect(() => parseTtsRequest({ text: 42 })).toThrow();
    expect(() => parseTtsRequest(null)).toThrow();
  });
  it("長すぎる text は弾く", () => {
    const long = "あ".repeat(MAX_TEXT_LENGTH + 1);
    expect(() => parseTtsRequest({ text: long })).toThrow();
  });
  it("不正な voice_id は弾く", () => {
    expect(() => parseTtsRequest({ text: "a", voice_id: -1 })).toThrow();
    expect(() => parseTtsRequest({ text: "a", voice_id: 1.5 })).toThrow();
    expect(() => parseTtsRequest({ text: "a", voice_id: "abc" })).toThrow();
  });
});

describe("adjustAudioQuery", () => {
  it("出力フォーマットを 16kHz/モノラルに上書きする", () => {
    const out = adjustAudioQuery({
      outputSamplingRate: 24000,
      outputStereo: true,
    });
    expect(out.outputSamplingRate).toBe(OUTPUT_SAMPLING_RATE);
    expect(out.outputStereo).toBe(OUTPUT_STEREO);
  });
  it("韻律など他フィールドは保持する", () => {
    const out = adjustAudioQuery({ speedScale: 1.2, accent_phrases: ["x"] });
    expect(out.speedScale).toBe(1.2);
    expect(out.accent_phrases).toEqual(["x"]);
  });
  it("元オブジェクトを破壊しない", () => {
    const src = { outputStereo: true };
    adjustAudioQuery(src);
    expect(src.outputStereo).toBe(true);
  });
});

describe("audioQueryUrl", () => {
  it("text と speaker をクエリに載せる", () => {
    const url = audioQueryUrl("http://localhost:50021", "やあ", 3);
    expect(url).toContain("http://localhost:50021/audio_query?");
    expect(url).toContain("speaker=3");
    expect(url).toContain(`text=${encodeURIComponent("やあ")}`);
  });
  it("ベース URL 末尾スラッシュを吸収する", () => {
    expect(audioQueryUrl("http://h:50021/", "a", 1)).toContain(
      "http://h:50021/audio_query?",
    );
  });
});

describe("synthesisUrl", () => {
  it("speaker をクエリに載せる", () => {
    expect(synthesisUrl("http://localhost:50021", 3)).toBe(
      "http://localhost:50021/synthesis?speaker=3",
    );
  });
});
