import { describe, it, expect } from "vitest";
import {
  adjustAudioQuery,
  audioQueryUrl,
  DEFAULT_SPEAKER,
  INTONATION_SCALE,
  MAX_TEXT_LENGTH,
  OUTPUT_SAMPLING_RATE,
  OUTPUT_STEREO,
  parseTtsRequest,
  synthesisUrl,
  ttsCacheKey,
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
  it("出力フォーマットを 24kHz/モノラルに上書きする", () => {
    const out = adjustAudioQuery({
      outputSamplingRate: 16000,
      outputStereo: true,
    });
    expect(out.outputSamplingRate).toBe(OUTPUT_SAMPLING_RATE);
    expect(OUTPUT_SAMPLING_RATE).toBe(24000);
    expect(out.outputStereo).toBe(OUTPUT_STEREO);
  });
  it("抑揚(intonationScale)を設定して平板さを減らす", () => {
    // 素の 1.0 を上書きし、控えめに強める（1.0 より大きく、誇張しすぎない範囲）。
    const out = adjustAudioQuery({ intonationScale: 1.0 });
    expect(out.intonationScale).toBe(INTONATION_SCALE);
    expect(INTONATION_SCALE).toBeGreaterThan(1.0);
    expect(INTONATION_SCALE).toBeLessThanOrEqual(1.3);
  });
  it("アクセント句など他の韻律フィールドは保持する", () => {
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

describe("ttsCacheKey", () => {
  it("同じ text/speaker は同じキーになる", () => {
    expect(ttsCacheKey("やあ", 3)).toBe(ttsCacheKey("やあ", 3));
  });
  it("text か speaker が違えば別キーになる", () => {
    expect(ttsCacheKey("やあ", 3)).not.toBe(ttsCacheKey("やあ", 1));
    expect(ttsCacheKey("やあ", 3)).not.toBe(ttsCacheKey("こんにちは", 3));
  });
  it("speaker と text の境界を取り違えない（区切りで衝突しない）", () => {
    // "3"+"あ" と "3あ" のような連結衝突が起きないこと。
    expect(ttsCacheKey("あ", 3)).not.toBe(ttsCacheKey("3あ", 0));
  });
  it("text 内にコロンを含んでも区切りと混同しない", () => {
    // 区切り文字がコロンなので、text 側のコロンで境界がずれないこと。
    expect(ttsCacheKey("a:b", 3)).not.toBe(ttsCacheKey("b", 3));
  });
});
