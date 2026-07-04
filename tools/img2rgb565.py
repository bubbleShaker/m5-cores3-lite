#!/usr/bin/env python3
"""PNG(透過あり) → RGB565 の C ヘッダ変換（実機の立ち絵アセット用）。

役割は「透過 PNG を、実機が pushImage でそのまま貼れる uint16_t の RGB565 配列にする」こと。
中継 /pokemon/sprite の rgbaToRgb565（relay/src/sprite.ts）と同じ設計方針をなぞる:
  - α が閾値未満の画素は「透過キー色」に潰す（RGB565 にはα値が無いためクロマキーで表現）。
  - 透過キーはポケスプライトと同じマゼンタ 0xF81F。実機は pushImage の transparent 引数へ同色を渡す。

ポケスプライトとの関係（重要）:
  - ここは uint16_t の「正しい RGB565 値」を並べる。これはポケ（中継の LE バイト列を実機で
    reinterpret_cast した後）が持つ uint16_t 値と同じ並び。
  - よって実機は pushImage 時に setSwapBytes(true) が必要（このパネルは送出時に上下バイトを
    入れ替える・#80 実機確認済み）。drawZunda も pokePushSprite と同じく true で貼る。

使い方:
  python tools/img2rgb565.py in.png --name kZundaClosed >> src/zunda_sprite.h
  # 複数フレームを1ヘッダにまとめたい場合は --name を変えて追記する。
"""
import argparse
import sys

from PIL import Image

# Windows の既定 stdout は cp932 等になり、生成ヘッダ内の日本語コメントが化ける。
# ヘッダはリポにコミットする成果物なので UTF-8 に固定する。
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

TRANSPARENT_KEY = 0xF81F  # マゼンタ（ポケスプライトと同じ透過キー）
ALPHA_THRESHOLD = 128     # これ未満のαは透過扱い


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    """8:8:8 → 5:6:5 に量子化して1つの uint16_t にパックする。"""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def convert(path: str, name: str) -> str:
    img = Image.open(path).convert("RGBA")
    w, h = img.size
    px = img.load()

    values = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < ALPHA_THRESHOLD:
                values.append(TRANSPARENT_KEY)
            else:
                v = rgb888_to_rgb565(r, g, b)
                # 不透明画素がたまたま透過キー色に量子化されると穴が空くので、隣値へ逃がす。
                if v == TRANSPARENT_KEY:
                    v ^= 0x0001
                values.append(v)

    out = []
    out.append(f"// {name}: {w}x{h} RGB565（透過キー=0x{TRANSPARENT_KEY:04X}）。生成: tools/img2rgb565.py")
    out.append(f"static constexpr int {name}W = {w};")
    out.append(f"static constexpr int {name}H = {h};")
    out.append(f"static const uint16_t {name}[{w * h}] = {{")
    # 1行 12 値で折り返す（差分レビューしやすい粒度）。
    for i in range(0, len(values), 12):
        chunk = values[i : i + 12]
        out.append("    " + ", ".join(f"0x{v:04X}" for v in chunk) + ",")
    out.append("};")
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description="PNG(RGBA) → RGB565 C header array")
    ap.add_argument("png", help="入力 PNG（RGBA・透過あり可）")
    ap.add_argument("--name", required=True, help="生成する配列名（例: kZundaClosed）")
    args = ap.parse_args()
    sys.stdout.write(convert(args.png, args.name))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
