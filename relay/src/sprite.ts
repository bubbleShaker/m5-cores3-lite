// 中継サーバ /pokemon/sprite の「純粋ロジック」。sharp（画像デコード）に依存せず vitest で完結する。
// 役割は3つ: (1) スプライト PNG の URL 組み立て (2) RGB565 変換 (3) RGBA raw → RGB565 バイト列化。
//
// sharp は「PNG をデコードして 96x96 の RGBA raw バイト列にする」トラスト境界として server.ts 側に置き、
// ここは「バイト列 → デバイスが pushImage で貼れる RGB565 バイト列」への純粋変換だけを担う。

// スプライトの一辺のピクセル数。PokeAPI の front_default は 96x96。
export const SPRITE_SIZE = 96;
// 出力バイト長。1 画素 2 バイト（RGB565）なので 96 * 96 * 2 = 18432。
export const SPRITE_BYTES = SPRITE_SIZE * SPRITE_SIZE * 2;

// 透過画素に割り当てるクロマキー色（RGB565）。0xF81F = マゼンタ。
// RGB565 にはα値が無いため、透明ピクセルをこの色へ潰し、
// デバイス側は pushImage の透過色オーバーロードでこの色を抜く（契約）。
// マゼンタは公式スプライトにほぼ出現しないため誤抜けが起きにくい。
export const TRANSPARENT_KEY = 0xf81f;
// この閾値未満のα値は「透明」とみなしクロマキー色にする。
export const ALPHA_THRESHOLD = 128;

// 8bit RGB(各0-255) を 16bit RGB565 へ。R5 G6 B5 に量子化して詰める。
// 上位から R(5) G(6) B(5)。値域外は下位ビット切り捨てで自然に収まる。
export function rgb565(r: number, g: number, b: number): number {
  const r5 = (r >> 3) & 0x1f;
  const g6 = (g >> 2) & 0x3f;
  const b5 = (b >> 3) & 0x1f;
  return (r5 << 11) | (g6 << 5) | b5;
}

// 末尾スラッシュの有無を吸収してベース URL を正規化する（pokemon.ts / tts.ts と同じ方針）。
function normalizeBase(baseUrl: string): string {
  return baseUrl.replace(/\/+$/, "");
}

// front_default スプライト PNG の URL を組み立てる。
// 既定ベースは PokeAPI/sprites リポジトリの GitHub raw CDN。
export function spriteUrl(baseUrl: string, id: number): string {
  return `${normalizeBase(baseUrl)}/${id}.png`;
}

// sharp が吐く RGBA raw（4ch・幅高さ SPRITE_SIZE）を RGB565 バイト列へ変換する純粋関数。
// バイト順はリトルエンディアン（下位バイト先）。ESP32 は LE なので
// デバイスは pushImage(x,y,w,h,(uint16_t*)buf) でバイトスワップ無しにそのまま貼れる（契約）。
// α<閾値の画素は TRANSPARENT_KEY に置換する。
export function rgbaToRgb565(rawRgba: Uint8Array): Uint8Array<ArrayBuffer> {
  const pixelCount = SPRITE_SIZE * SPRITE_SIZE;
  if (rawRgba.length !== pixelCount * 4) {
    throw new Error(
      `raw RGBA length must be ${pixelCount * 4} (got ${rawRgba.length})`,
    );
  }

  // 専用 ArrayBuffer で確保する（Hono c.body へそのまま渡せる型・全バイト後で埋める）。
  const out = new Uint8Array(SPRITE_BYTES);
  for (let i = 0; i < pixelCount; i++) {
    const src = i * 4; // RGBA 各1バイト
    const a = rawRgba[src + 3];
    const value =
      a < ALPHA_THRESHOLD
        ? TRANSPARENT_KEY
        : rgb565(rawRgba[src], rawRgba[src + 1], rawRgba[src + 2]);

    const dst = i * 2;
    out[dst] = value & 0xff; // 下位バイト先（リトルエンディアン）
    out[dst + 1] = (value >> 8) & 0xff; // 上位バイト
  }
  return out;
}
