import { describe, it, expect } from "vitest";
import { runTranscode } from "../src/transcode";

// 実 FFmpeg の代わりに標準 UNIX コマンドをフェイク変換器として使い、
// child_process の各分岐（成功 / 非ゼロ終了 / spawn失敗 / タイムアウト / サイズ超過）を検証する。
const base = { maxOutputBytes: 1024 * 1024, timeoutMs: 2000 };

describe("runTranscode", () => {
  it("成功: cat は stdin をそのまま stdout へ返す", async () => {
    const input = new Uint8Array([1, 2, 3, 4, 5]);
    const out = await runTranscode(input, { bin: "cat", args: [], ...base });
    expect(Array.from(out)).toEqual(Array.from(input));
  });

  it("非ゼロ終了: false は reject する", async () => {
    // false は stdin を読まず即終了するため、非ゼロ終了(exited) か
    // stdin 書き込みの EPIPE のどちらかで reject する（いずれも失敗として正しい）。
    await expect(
      runTranscode(new Uint8Array([0]), { bin: "false", args: [], ...base }),
    ).rejects.toThrow();
  });

  it("spawn 失敗: 存在しないコマンドは reject する", async () => {
    await expect(
      runTranscode(new Uint8Array([0]), {
        bin: "definitely-not-a-real-binary-xyz",
        args: [],
        ...base,
      }),
    ).rejects.toThrow();
  });

  it("サイズ超過: 出力が上限を超えると reject する", async () => {
    // cat で 100 バイト返すが上限を 10 バイトに絞る。
    const input = new Uint8Array(100).fill(7);
    await expect(
      runTranscode(input, {
        bin: "cat",
        args: [],
        maxOutputBytes: 10,
        timeoutMs: 2000,
      }),
    ).rejects.toThrow(/size limit/);
  });

  it("タイムアウト: 無応答プロセスは打ち切って reject する", async () => {
    // sleep は stdin を読まず出力もしないので、タイマーだけが解決する。
    await expect(
      runTranscode(new Uint8Array([0]), {
        bin: "sleep",
        args: ["5"],
        maxOutputBytes: 1024,
        timeoutMs: 150,
      }),
    ).rejects.toThrow(/timed out/);
  });
});
