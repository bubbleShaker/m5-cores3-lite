// 実行アダプタの「純粋」な半分（#219）。OS も fs も触らないので vitest で完結する。
//
// 役割は 2 つ:
//   (1) apps.json に書かれた実行パスが**そもそも起動してよい形か**を検証する
//   (2) PowerShell へ渡す引数配列を組み立てる（文字列連結でコマンドを作らない）
//
// ここが「LLM の幻覚」ではなく「設定ファイルの書き間違い・書き換え」に対する防壁になる。
// M2（tools.ts）は LLM 由来の入力を、この層は**設定ファイル由来の入力**を疑う。
import { win32 } from "node:path";

// Windows の絶対パス（ドライブレター始まり）だけ許す。
const ABSOLUTE_RE = /^[A-Za-z]:[\\/]/;

// 実行ファイル名として許す形。`Get-Process -Name` と PowerShell の引数束縛に渡るので、
// **先頭が `-` になり得ない**ことが重要（`-Command` 等のパラメータに化ける余地を消す）。
const BASENAME_RE = /^[A-Za-z0-9][A-Za-z0-9 ._-]{0,63}$/;

// パスに現れてはいけない文字。
//   %      … cmd の環境変数展開（%WINDIR% 等）
//   ` $    … PowerShell の展開・部分式
//   " < > | ? *  … Windows のパスに使えない文字（リダイレクト等に見える形も混ぜない）
// シェルを介さずに起動するので本来は無害だが、多層防御として入口で落とす。
const FORBIDDEN_CHARS_RE = /[%`$"<>|?*]/;

// 起動を許す拡張子は .exe だけ。
// ⚠ .bat / .cmd を許すと Node は Windows で**シェル経由でしか起動できない**
// （CVE-2024-27980 の緩和として spawn が shell 必須にした）。つまり許した瞬間に
// 「シェルを介さない」という前提が崩れる。ショートカット(.lnk)も同じ理由で許さない。
const ALLOWED_EXT = ".exe";

// PowerShell 実行ファイル。Windows に必ず入っている Windows PowerShell 5.1 を既定にする。
export const POWERSHELL_BIN_DEFAULT = "powershell.exe";

// PowerShell へ渡す固定引数。
//   -NoProfile      … 利用者のプロファイル（任意コードが書ける）を読ませない
//   -NonInteractive … プロンプトで固まらせない
//   -File           … **-Command ではなく -File**。渡した値はコードではなくパラメータの値として束縛される
export const POWERSHELL_FIXED_ARGS = [
  "-NoProfile",
  "-NonInteractive",
  "-ExecutionPolicy",
  "Bypass",
  "-File",
] as const;

// window.ps1 が返す終了コード。3 は「そのアプリの窓が無い」＝失敗ではなく状態。
export const EXIT_OK = 0;
export const EXIT_NO_WINDOW = 3;

// ps1 に渡す操作名。ToolCall の state に focus を足しただけ（focus_window 用）。
export const WINDOW_ACTIONS = ["minimize", "maximize", "restore", "focus"] as const;
export type WindowAction = (typeof WINDOW_ACTIONS)[number];

// apps.json の値を「起動してよい実行パス」へ正規化する。**不正なら投げる**（起動時に落とす）。
// ここを通らない値は spawn に渡らない。
export function validateAppPath(raw: string): string {
  const value = raw.trim();
  if (value === "") throw new Error("実行パスが空");
  if (value.length > 512) throw new Error("実行パスが長すぎる");
  if (FORBIDDEN_CHARS_RE.test(value)) {
    throw new Error("実行パスに使えない文字が含まれる（環境変数展開・シェル記法は不可）");
  }
  if (/\$env:/i.test(value)) throw new Error("実行パスに環境変数展開が含まれる");
  // UNC（\\server\share）は名前解決の先が外部ホストになり得るので許さない。
  if (value.startsWith("\\\\") || value.startsWith("//")) {
    throw new Error("UNC パスは許可しない");
  }
  if (!ABSOLUTE_RE.test(value)) throw new Error("絶対パスではない");
  // ⚠ normalize より**前**に見る。normalize は `..` を畳んでしまい、
  // 「a/../b」が素通りしたのか元から b だったのか区別できなくなる。
  const segments = value.split(/[\\/]/);
  if (segments.includes("..")) throw new Error("実行パスに .. を含められない");

  const normalized = win32.normalize(value);
  if (win32.extname(normalized).toLowerCase() !== ALLOWED_EXT) {
    throw new Error(`起動できるのは ${ALLOWED_EXT} だけ`);
  }
  const base = win32.basename(normalized);
  if (!BASENAME_RE.test(base)) {
    throw new Error("実行ファイル名に使えない文字が含まれる");
  }
  return normalized;
}

// 実行パス → `Get-Process -Name` に渡すプロセス名（拡張子なしのファイル名）。
// 検証済みパスを前提にするので、ここでは形の心配をしなくてよい。
export function processNameOf(validatedPath: string): string {
  return win32.basename(validatedPath, ALLOWED_EXT);
}

// ToolCall の種類 → ps1 の -Action 値。
export function windowActionOf(
  name: "focus_window" | "window_state",
  state?: string,
): WindowAction {
  if (name === "focus_window") return "focus";
  // state は tools.ts で列挙値に検証済み。ここは念のための最終防壁。
  if ((WINDOW_ACTIONS as readonly string[]).includes(state ?? "")) {
    return state as WindowAction;
  }
  throw new Error(`未知のウィンドウ操作`);
}

// PowerShell へ渡す引数**配列**を組み立てる。
// 文字列を 1 本に連結しないのが要点。連結した瞬間に「どこまでが値か」を
// PowerShell のパーサが決めることになり、値がコードに化ける余地が生まれる。
export function windowScriptArgs(
  scriptPath: string,
  processName: string,
  action: WindowAction,
): string[] {
  if (!BASENAME_RE.test(processName)) {
    throw new Error("プロセス名が不正");
  }
  if (!(WINDOW_ACTIONS as readonly string[]).includes(action)) {
    throw new Error("未知のウィンドウ操作");
  }
  return [
    ...POWERSHELL_FIXED_ARGS,
    scriptPath,
    "-ProcessName",
    processName,
    "-Action",
    action,
  ];
}
