import { describe, it, expect } from "vitest";
import {
  BITS_PER_SAMPLE,
  CHANNELS,
  cryUrl,
  ffmpegArgs,
  SAMPLE_RATE,
  WAV_HEADER_SIZE,
  writeWavHeader,
} from "../src/cry";

// バイト列から LE で数値を読むヘルパ（テスト側の検証用）。
const u32 = (b: Uint8Array, o: number) =>
  b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24);
const u16 = (b: Uint8Array, o: number) => b[o] | (b[o + 1] << 8);
const tag = (b: Uint8Array, o: number) =>
  String.fromCharCode(b[o], b[o + 1], b[o + 2], b[o + 3]);

describe("cryUrl", () => {
  it("id.ogg をパスに載せる", () => {
    expect(
      cryUrl(
        "https://raw.githubusercontent.com/PokeAPI/cries/main/cries/pokemon/latest",
        25,
      ),
    ).toBe(
      "https://raw.githubusercontent.com/PokeAPI/cries/main/cries/pokemon/latest/25.ogg",
    );
  });
  it("ベース URL 末尾スラッシュを吸収する", () => {
    expect(cryUrl("https://cdn/x/", 1)).toBe("https://cdn/x/1.ogg");
  });
});

describe("ffmpegArgs", () => {
  it("stdin の OGG を 16kHz/mono/生PCM(s16le) で stdout へ出す引数", () => {
    const args = ffmpegArgs();
    expect(args).toContain("pipe:0");
    expect(args).toContain("pipe:1");
    // -ar 16000 / -ac 1 / -f s16le が並ぶ。
    expect(args[args.indexOf("-ar") + 1]).toBe(String(SAMPLE_RATE));
    expect(args[args.indexOf("-ac") + 1]).toBe(String(CHANNELS));
    expect(args[args.indexOf("-f") + 1]).toBe("s16le");
  });
  it("-f wav を使わない（パイプ出力でヘッダが壊れるため）", () => {
    expect(ffmpegArgs()).not.toContain("wav");
  });
});

describe("writeWavHeader", () => {
  const pcm = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]); // 8 バイトの擬似 PCM

  it("出力長はヘッダ44 + PCM 長", () => {
    expect(writeWavHeader(pcm).length).toBe(WAV_HEADER_SIZE + pcm.length);
  });

  it("RIFF/WAVE/fmt /data のマジックを正しく書く", () => {
    const w = writeWavHeader(pcm);
    expect(tag(w, 0)).toBe("RIFF");
    expect(tag(w, 8)).toBe("WAVE");
    expect(tag(w, 12)).toBe("fmt ");
    expect(tag(w, 36)).toBe("data");
  });

  it("RIFF サイズは 36 + データ長", () => {
    const w = writeWavHeader(pcm);
    expect(u32(w, 4)).toBe(36 + pcm.length);
  });

  it("fmt フィールドが 16kHz/mono/16bit/PCM を表す", () => {
    const w = writeWavHeader(pcm);
    expect(u32(w, 16)).toBe(16); // fmt チャンク長
    expect(u16(w, 20)).toBe(1); // PCM
    expect(u16(w, 22)).toBe(CHANNELS);
    expect(u32(w, 24)).toBe(SAMPLE_RATE);
    expect(u32(w, 28)).toBe((SAMPLE_RATE * CHANNELS * BITS_PER_SAMPLE) / 8); // byteRate
    expect(u16(w, 32)).toBe((CHANNELS * BITS_PER_SAMPLE) / 8); // blockAlign
    expect(u16(w, 34)).toBe(BITS_PER_SAMPLE);
  });

  it("data サイズは PCM 長そのもの（プレースホルダでない）", () => {
    const w = writeWavHeader(pcm);
    expect(u32(w, 40)).toBe(pcm.length);
  });

  it("PCM 本体がヘッダ直後にそのまま入る", () => {
    const w = writeWavHeader(pcm);
    expect(Array.from(w.slice(WAV_HEADER_SIZE))).toEqual(Array.from(pcm));
  });

  it("空 PCM でも妥当な 44 バイト WAV を作る", () => {
    const w = writeWavHeader(new Uint8Array(0));
    expect(w.length).toBe(WAV_HEADER_SIZE);
    expect(u32(w, 40)).toBe(0);
    expect(tag(w, 0)).toBe("RIFF");
  });
});
