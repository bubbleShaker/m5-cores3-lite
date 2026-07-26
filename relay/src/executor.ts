// 実行アダプタ（#219）。**ここが唯一 OS に触る層**で、クリーンアーキテクチャ上は最も外側。
// 純粋ロジック（tools.ts / winexec.ts）はこのファイルを知らないし依存もしない。
//
// 守っている不変条件:
//   1. シェルを介さない（`shell: false`・`exec` 系は使わない）。文字列連結でコマンドを作らない
//   2. 起動する実行ファイルは apps.json のキーで引いた値だけ。**LLM は引数を渡せない**
//      （spawn の引数配列は常に空。ここに何かを流し込む経路自体を作らない）
//   3. どんな失敗も例外で外へ漏らさない。必ず ExecOutcome へ写像して呼び出し側へ返す
import { spawn as nodeSpawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { safeLabel, type ToolCall } from "./tools";
import {
  POWERSHELL_BIN_DEFAULT,
  EXIT_NO_WINDOW,
  EXIT_OK,
  processNameOf,
  validateAppPath,
  windowActionOf,
  windowScriptArgs,
} from "./winexec";

// 実行結果。denied は「実行しなかった」、failed は「実行しようとして駄目だった」。
// not_running は「そのアプリの窓がまだ無い」＝異常ではないので言い方を分ける。
export type ExecOutcome = "ok" | "denied" | "failed" | "not_running";

export interface ExecResult {
  outcome: ExecOutcome;
  // ログ用の短い理由。ユーザーへ喋る文はここから作らない（toolSpeech の定型文を使う）。
  detail: string;
}

// spawn を差し替え可能にするための最小のインターフェース。
// これがあるおかげで、テストは**実際にアプリを起動せずに**「何を spawn したか」を検証できる。
export interface SpawnedProcess {
  on(event: string, listener: (...args: never[]) => void): unknown;
  unref(): void;
  kill(signal?: NodeJS.Signals | number): boolean;
}

export interface SpawnOptionsLike {
  shell: false;
  stdio: "ignore";
  detached?: boolean;
  windowsHide?: boolean;
}

export type SpawnFn = (
  command: string,
  args: readonly string[],
  options: SpawnOptionsLike,
) => SpawnedProcess;

export interface ExecutorOptions {
  spawn: SpawnFn;
  powershell: string;
  scriptPath: string;
  timeoutMs: number;
}

export interface Executor {
  execute(call: ToolCall): Promise<ExecResult>;
}

// window.ps1 の所在。src/ からの相対で解決する（起動ディレクトリに依存させない）。
export const DEFAULT_SCRIPT_PATH = fileURLToPath(
  new URL("../scripts/window.ps1", import.meta.url),
);

// PowerShell が無応答になっても /chat を道連れにしないための門番。
export const DEFAULT_TIMEOUT_MS = 10_000;

export function createExecutor(
  apps: Readonly<Record<string, string>>,
  options: Partial<ExecutorOptions> = {},
): Executor {
  const opts: ExecutorOptions = {
    spawn: nodeSpawn as unknown as SpawnFn,
    powershell: POWERSHELL_BIN_DEFAULT,
    scriptPath: DEFAULT_SCRIPT_PATH,
    timeoutMs: DEFAULT_TIMEOUT_MS,
    ...options,
  };

  // 検証済みのキー → 実行パス。apps は Object.create(null) 由来だが、
  // 呼び出し元を限定しないためプロトタイプ経由の値を拾わない形で引く。
  function lookup(key: string): string | undefined {
    return Object.prototype.hasOwnProperty.call(apps, key) ? apps[key] : undefined;
  }

  // アプリを起動する。**引数は常に空配列**。
  // ⚠ ここに引数を通す口を作ると、許可リストが一瞬で無意味になる
  //   （例: terminal に "-c ..." を渡せたら任意コマンド実行と同じ）。
  function launch(exePath: string): Promise<ExecResult> {
    return new Promise((resolve) => {
      let settled = false;
      const done = (result: ExecResult) => {
        if (!settled) {
          settled = true;
          resolve(result);
        }
      };
      let child: SpawnedProcess;
      try {
        child = opts.spawn(exePath, [], {
          shell: false, // ← シェルを介さない。CreateProcess は %VAR% を展開しない
          stdio: "ignore",
          detached: true, // relay を落としてもアプリは生き残る（逆に relay が待ち続けない）
          windowsHide: false, // 起動したアプリの窓は見えてよい
        });
      } catch (err) {
        return done({ outcome: "failed", detail: safeLabel(String((err as Error).message)) });
      }
      // spawn 失敗（ENOENT 等）は例外ではなく error イベントで来る。握って failed に写像する。
      child.on("error", ((err: Error) =>
        done({ outcome: "failed", detail: safeLabel(err.message) })) as never);
      // spawn イベント＝子プロセスの生成に成功した瞬間。
      child.on("spawn", (() => {
        child.unref(); // 参照を切る。切らないと relay のイベントループが子に縛られる
        done({ outcome: "ok", detail: "" });
      }) as never);
    });
  }

  // window.ps1 を実行し、終了コードを ExecOutcome へ写像する。
  function runWindowScript(args: readonly string[]): Promise<ExecResult> {
    return new Promise((resolve) => {
      let settled = false;
      let timer: NodeJS.Timeout | undefined;
      const done = (result: ExecResult) => {
        if (settled) return;
        settled = true;
        if (timer) clearTimeout(timer);
        resolve(result);
      };
      let child: SpawnedProcess;
      try {
        child = opts.spawn(opts.powershell, args, {
          shell: false,
          stdio: "ignore",
          windowsHide: true, // PowerShell の黒窓を出さない
        });
      } catch (err) {
        return done({ outcome: "failed", detail: safeLabel(String((err as Error).message)) });
      }
      timer = setTimeout(() => {
        child.kill();
        done({ outcome: "failed", detail: "タイムアウト" });
      }, opts.timeoutMs);
      // タイマーがプロセス終了を引き止めないようにする（テストの終了も速くなる）。
      timer.unref?.();

      child.on("error", ((err: Error) =>
        done({ outcome: "failed", detail: safeLabel(err.message) })) as never);
      child.on("exit", ((code: number | null) => {
        if (code === EXIT_OK) return done({ outcome: "ok", detail: "" });
        // 「窓が無い」は失敗ではなく状態。ユーザーには別の文で伝える。
        if (code === EXIT_NO_WINDOW) {
          return done({ outcome: "not_running", detail: "窓が見つからない" });
        }
        done({ outcome: "failed", detail: `powershell exit=${code}` });
      }) as never);
    });
  }

  return {
    async execute(call: ToolCall): Promise<ExecResult> {
      if (call.name === "reply_only") return { outcome: "ok", detail: "" };

      // 許可リストに無いキーは**実行経路に入る前**で止める。
      // tools.ts でも弾いているが、ここが最後の門なので二重に持つ（片方の改変で穴が開かない）。
      const raw = lookup(call.app);
      if (raw === undefined) {
        return { outcome: "denied", detail: `未登録のアプリ: ${safeLabel(call.app)}` };
      }

      // 設定ファイル由来の値も信用しない。起動時に検証済みでも、ここで必ずもう一度通す。
      let exePath: string;
      try {
        exePath = validateAppPath(raw);
      } catch (err) {
        return { outcome: "denied", detail: safeLabel((err as Error).message) };
      }

      if (call.name === "launch_app") {
        return launch(exePath);
      }

      try {
        const action = windowActionOf(
          call.name,
          call.name === "window_state" ? call.state : undefined,
        );
        const args = windowScriptArgs(opts.scriptPath, processNameOf(exePath), action);
        return await runWindowScript(args);
      } catch (err) {
        return { outcome: "denied", detail: safeLabel((err as Error).message) };
      }
    },
  };
}
