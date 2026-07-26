// scripts/window.ps1 のスモークテスト（#219）。**副作用ゼロ**の経路だけを実際に走らせる。
//
// なぜ要るか: この ps1 は「どの窓に手を出すか」を決めるロジック（実行パス一致 →
// 名前一致フォールバック）とパラメータの束縛防壁を持つのに、TypeScript 側のテストからは
// 中身が一切見えない。BOM の有無だけを見るテストでは
// 「BOM はあるが構文が壊れた」「ValidateSet が外れた」を検出できない。
//
// 対象プロセスが存在しないケースだけを使うので、実行してもウィンドウは 1 つも動かない。
import { describe, it, expect } from "vitest";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { powerShellPath } from "../src/winexec";

const SCRIPT = fileURLToPath(new URL("../scripts/window.ps1", import.meta.url));
const onWindows = process.platform === "win32";

function run(args: string[]): number | null {
  const res = spawnSync(
    powerShellPath(process.env.SystemRoot),
    ["-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", SCRIPT, ...args],
    { stdio: "ignore", timeout: 30_000, shell: false },
  );
  return res.status;
}

describe.runIf(onWindows)("scripts/window.ps1", () => {
  it("存在しないプロセスなら exit 3（＝not_running）", () => {
    // ここが 1 になる時は構文エラー（BOM 消失で日本語コメントが化けた等）を疑う。
    expect(
      run([
        "-ProcessName",
        "relay-no-such-process",
        "-ExePath",
        "C:\\Windows\\System32\\relay-no-such-process.exe",
        "-Action",
        "minimize",
      ]),
    ).toBe(3);
  }, 40_000);

  it("列挙外の -Action は束縛の時点で失敗する（実行されない）", () => {
    expect(
      run([
        "-ProcessName",
        "relay-no-such-process",
        "-ExePath",
        "C:\\Windows\\System32\\relay-no-such-process.exe",
        "-Action",
        "close",
      ]),
    ).not.toBe(0);
  }, 40_000);

  it("不正な -ProcessName は束縛の時点で失敗する", () => {
    expect(
      run([
        "-ProcessName",
        "zzz; whoami",
        "-ExePath",
        "C:\\Windows\\System32\\relay-no-such-process.exe",
        "-Action",
        "minimize",
      ]),
    ).not.toBe(0);
  }, 40_000);
});
