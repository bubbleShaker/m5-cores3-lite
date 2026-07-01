import { describe, it, expect } from "vitest";
import {
  ALPHA_THRESHOLD,
  rgb565,
  rgbaToRgb565,
  SPRITE_BYTES,
  SPRITE_SIZE,
  spriteUrl,
  TRANSPARENT_KEY,
} from "../src/sprite";

describe("rgb565", () => {
  it("純白は 0xFFFF", () => {
    expect(rgb565(255, 255, 255)).toBe(0xffff);
  });
  it("純黒は 0x0000", () => {
    expect(rgb565(0, 0, 0)).toBe(0x0000);
  });
  it("純赤は R5 が最大（0xF800）", () => {
    expect(rgb565(255, 0, 0)).toBe(0xf800);
  });
  it("純緑は G6 が最大（0x07E0）", () => {
    expect(rgb565(0, 255, 0)).toBe(0x07e0);
  });
  it("純青は B5 が最大（0x001F）", () => {
    expect(rgb565(0, 0, 255)).toBe(0x001f);
  });
  it("下位ビットは量子化で切り捨てられる", () => {
    // R の下位3bit・G の下位2bit・B の下位3bit は捨てられる。
    expect(rgb565(0b00000111, 0b00000011, 0b00000111)).toBe(0x0000);
  });
});

describe("spriteUrl", () => {
  it("id.png をパスに載せる", () => {
    expect(
      spriteUrl(
        "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon",
        25,
      ),
    ).toBe(
      "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/25.png",
    );
  });
  it("ベース URL 末尾スラッシュを吸収する", () => {
    expect(spriteUrl("https://cdn/x/", 1)).toBe("https://cdn/x/1.png");
  });
});

describe("rgbaToRgb565", () => {
  // 全画素同色の RGBA raw を作るヘルパ。
  const fill = (r: number, g: number, b: number, a: number): Uint8Array => {
    const buf = new Uint8Array(SPRITE_SIZE * SPRITE_SIZE * 4);
    for (let i = 0; i < buf.length; i += 4) {
      buf[i] = r;
      buf[i + 1] = g;
      buf[i + 2] = b;
      buf[i + 3] = a;
    }
    return buf;
  };

  it("出力は常に SPRITE_BYTES(18432) バイト", () => {
    expect(rgbaToRgb565(fill(255, 0, 0, 255)).length).toBe(SPRITE_BYTES);
  });

  it("不正な raw 長は弾く", () => {
    expect(() => rgbaToRgb565(new Uint8Array(10))).toThrow();
  });

  it("不透明画素はリトルエンディアンで格納される（純赤 0xF800 → 00 F8）", () => {
    const out = rgbaToRgb565(fill(255, 0, 0, 255));
    expect(out[0]).toBe(0x00); // 下位バイト先
    expect(out[1]).toBe(0xf8); // 上位バイト
  });

  it("α<閾値の画素は TRANSPARENT_KEY(0xF81F) に置換される", () => {
    const out = rgbaToRgb565(fill(10, 20, 30, ALPHA_THRESHOLD - 1));
    // 0xF81F をリトルエンディアンで: 1F F8
    expect(out[0]).toBe(TRANSPARENT_KEY & 0xff);
    expect(out[1]).toBe((TRANSPARENT_KEY >> 8) & 0xff);
  });

  it("α>=閾値なら透過キーにせず実際の色を使う", () => {
    const out = rgbaToRgb565(fill(255, 255, 255, ALPHA_THRESHOLD));
    // 純白 0xFFFF → FF FF（キー色ではない）
    expect(out[0]).toBe(0xff);
    expect(out[1]).toBe(0xff);
  });
});
