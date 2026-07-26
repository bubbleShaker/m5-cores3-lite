// 実行パス検証と PowerShell 引数組み立ての単体テスト（#219）。
// ここが「設定ファイルに変な値を書いても外に効かない」ことの担保。
import { describe, it, expect } from "vitest";
import {
  POWERSHELL_FIXED_ARGS,
  processNameOf,
  validateAppPath,
  windowActionOf,
  windowScriptArgs,
} from "../src/winexec";

describe("validateAppPath", () => {
  it("正しい絶対パスを受け付け、区切りを正規化する", () => {
    expect(validateAppPath("C:/Program Files (x86)/Microsoft/Edge/msedge.exe")).toBe(
      "C:\\Program Files (x86)\\Microsoft\\Edge\\msedge.exe",
    );
    expect(validateAppPath("  C:\\Windows\\notepad.exe  ")).toBe("C:\\Windows\\notepad.exe");
  });

  // ここから下が Issue のセキュリティ要件（設定ファイルの値が外に効かないこと）。
  it("`..` を含むパスを拒否する（正規化で消える前に見る）", () => {
    expect(() => validateAppPath("C:/apps/../Windows/System32/cmd.exe")).toThrow(/\.\./);
    expect(() => validateAppPath("C:\\apps\\..\\evil.exe")).toThrow(/\.\./);
  });

  it("環境変数展開の記法を拒否する", () => {
    expect(() => validateAppPath("%WINDIR%/System32/cmd.exe")).toThrow();
    expect(() => validateAppPath("C:/%WINDIR%/cmd.exe")).toThrow();
    expect(() => validateAppPath("C:/x/$env:WINDIR.exe")).toThrow();
    expect(() => validateAppPath("C:/x/`whoami`.exe")).toThrow();
    expect(() => validateAppPath("C:/x/$(whoami).exe")).toThrow();
  });

  it("相対パス・UNC パスを拒否する", () => {
    expect(() => validateAppPath("msedge.exe")).toThrow(/絶対パス/);
    expect(() => validateAppPath("./msedge.exe")).toThrow(/絶対パス/);
    expect(() => validateAppPath("\\\\server\\share\\app.exe")).toThrow(/UNC/);
    expect(() => validateAppPath("//server/share/app.exe")).toThrow(/UNC/);
  });

  it(".exe 以外を拒否する（.bat/.cmd はシェル経由になるため特に危険）", () => {
    expect(() => validateAppPath("C:/x/run.bat")).toThrow(/\.exe/);
    expect(() => validateAppPath("C:/x/run.cmd")).toThrow(/\.exe/);
    expect(() => validateAppPath("C:/x/run.ps1")).toThrow(/\.exe/);
    expect(() => validateAppPath("C:/x/link.lnk")).toThrow(/\.exe/);
    expect(() => validateAppPath("C:/x/nodots")).toThrow(/\.exe/);
  });

  it("PowerShell のパラメータに化ける実行ファイル名を拒否する", () => {
    // 先頭が `-` だと `-Command` 等のパラメータとして解釈される余地が残る。
    expect(() => validateAppPath("C:/x/-Command.exe")).toThrow(/実行ファイル名/);
  });

  it("空・長すぎる値を拒否する", () => {
    expect(() => validateAppPath("   ")).toThrow(/空/);
    expect(() => validateAppPath(`C:/${"a".repeat(600)}.exe`)).toThrow(/長すぎる/);
  });
});

describe("processNameOf", () => {
  it("拡張子を落としたファイル名を返す（Get-Process -Name 用）", () => {
    expect(processNameOf("C:\\Program Files\\Edge\\msedge.exe")).toBe("msedge");
    expect(processNameOf("C:\\x\\Code.exe")).toBe("Code");
  });
});

describe("windowActionOf", () => {
  it("focus_window は focus、window_state は state をそのまま使う", () => {
    expect(windowActionOf("focus_window")).toBe("focus");
    expect(windowActionOf("window_state", "minimize")).toBe("minimize");
    expect(windowActionOf("window_state", "maximize")).toBe("maximize");
    expect(windowActionOf("window_state", "restore")).toBe("restore");
  });

  it("列挙外の state は投げる（最終防壁）", () => {
    expect(() => windowActionOf("window_state", "close")).toThrow();
    expect(() => windowActionOf("window_state")).toThrow();
  });
});

describe("windowScriptArgs", () => {
  it("引数を配列で組み立てる（文字列連結しない）", () => {
    const args = windowScriptArgs("C:\\relay\\scripts\\window.ps1", "msedge", "minimize");
    expect(args).toEqual([
      ...POWERSHELL_FIXED_ARGS,
      "C:\\relay\\scripts\\window.ps1",
      "-ProcessName",
      "msedge",
      "-Action",
      "minimize",
    ]);
    // -Command を使っていないこと（使うと値がコードとして評価され得る）。
    expect(args).not.toContain("-Command");
    expect(args).toContain("-NoProfile");
    expect(args).toContain("-NonInteractive");
  });

  it("不正なプロセス名・操作名は投げる", () => {
    expect(() => windowScriptArgs("s.ps1", "-Command", "minimize")).toThrow(/プロセス名/);
    expect(() => windowScriptArgs("s.ps1", "msedge; whoami", "minimize")).toThrow(/プロセス名/);
    expect(() =>
      windowScriptArgs("s.ps1", "msedge", "close" as never),
    ).toThrow(/ウィンドウ操作/);
  });
});
