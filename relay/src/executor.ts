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
import { safeLabel, type ToolCall, type ToolOutcome } from "./tools";
import {
  EXIT_NO_WINDOW,
  EXIT_OK,
  EXIT_OK_BY_NAME,
  powerShellPath,
  validateAppPath,
  windowActionOf,
  windowScriptArgs,
} from "./winexec";

// 実行結果。denied は「実行しなかった」、failed は「実行しようとして駄目だった」。
// not_running は「そのアプリの窓がまだ無い」、busy は「他の操作が走っていた」。
// ⚠ busy を denied に混ぜない。「攻撃/誤作動で止めた」と「混んでいただけ」が
// 監査ログ上で同じ記号になると、この記録の目的（誤作動の切り分け）が薄れる。
export type ExecOutcome = ToolOutcome;

export interface ExecResult {
  outcome: ExecOutcome;
  // ログ用の短い理由。ユーザーへ喋る文はここから作らない（toolSpeech の定型文を使う）。
  detail: string;
}

// spawn を差し替え可能にするための最小のインターフェース。
// これがあるおかげで、テストは**実際にアプリを起動せずに**「何を spawn したか」を検証できる。
//
// ⚠ event を string ではなくユニオンで縛る。string のままだと "spwan" のような打ち間違いが
// 型検査を素通りし、**リスナーが誰にも呼ばれず /chat が永久に待つ**（無言でハングする）。
export interface SpawnedProcess {
  on(event: "error", listener: (err: Error) => void): unknown;
  on(event: "spawn", listener: () => void): unknown;
  on(event: "exit", listener: (code: number | null) => void): unknown;
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

// SystemRoot が壊れていても既定の場所へフォールバックする。
function safePowerShellPath(): string {
  try {
    return powerShellPath(process.env.SystemRoot);
  } catch {
    return powerShellPath("C:\\Windows");
  }
}

export function createExecutor(
  apps: Readonly<Record<string, string>>,
  options: Partial<ExecutorOptions> = {},
): Executor {
  const opts: ExecutorOptions = {
    spawn: nodeSpawn as unknown as SpawnFn,
    // SystemRoot が異常な環境でも起動そのものは止めない（対話は使えるべき）。
    // ここで投げるとトップレベル未捕捉例外になり、server.ts の「relay refused to start: 」の
    // 体裁を通らずに落ちてしまう。
    powershell: safePowerShellPath(),
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
      const { done } = settler(resolve);
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
      // spawn / error のどちらも来ない事故（プラットフォーム差・差し替えた spawn の不備）で
      // /chat が永久に待たないよう、ここにも門番を置く。
      // ⚠ ここでは kill しない。対象は**ユーザーが今開こうとしたアプリ自身**なので、
      // 開きかけの窓を relay が勝手に閉じる方が驚きが大きい（PowerShell とは性質が違う）。
      const timer = armTimeout(child, done, { kill: false });
      // spawn 失敗（ENOENT 等）は例外ではなく error イベントで来る。握って failed に写像する。
      child.on("error", (err: Error) => {
        clearTimeout(timer);
        done({ outcome: "failed", detail: safeLabel(err.message) });
      });
      // spawn イベント＝子プロセスの生成に成功した瞬間。
      // ⚠ ここでは「起動できた」までしか分からない。直後にアプリが自分で落ちても ok を返す。
      child.on("spawn", () => {
        clearTimeout(timer);
        child.unref(); // 参照を切る。切らないと relay のイベントループが子に縛られる
        done({ outcome: "ok", detail: "" });
      });
    });
  }

  // window.ps1 を実行し、終了コードを ExecOutcome へ写像する。
  function runWindowScript(args: readonly string[]): Promise<ExecResult> {
    return new Promise((resolve) => {
      const { done } = settler(resolve);
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
      const timer = armTimeout(child, done);

      child.on("error", (err: Error) => {
        clearTimeout(timer);
        done({ outcome: "failed", detail: safeLabel(err.message) });
      });
      child.on("exit", (code: number | null) => {
        clearTimeout(timer);
        if (code === EXIT_OK) return done({ outcome: "ok", detail: "" });
        // 実行パスは一致しなかったが、名前一致の候補が 1 つだけだったので操作した。
        // 成功ではあるが「登録した実行ファイルそのもの」だとは確認できていないので記録に残す。
        if (code === EXIT_OK_BY_NAME) {
          return done({ outcome: "ok", detail: "名前一致で実行（実行パス不一致）" });
        }
        // 「窓が無い」は失敗ではなく状態。ユーザーには別の文で伝える。
        if (code === EXIT_NO_WINDOW) {
          return done({ outcome: "not_running", detail: "窓が見つからない" });
        }
        done({ outcome: "failed", detail: `powershell exit=${code}` });
      });
    });
  }

  // 「最初の 1 回だけ resolve する」を共通化する。
  function settler(resolve: (r: ExecResult) => void) {
    let settled = false;
    return {
      done(result: ExecResult) {
        if (settled) return;
        settled = true;
        resolve(result);
      },
    };
  }

  // 無応答の子プロセスを見捨てる門番。
  function armTimeout(
    child: SpawnedProcess,
    done: (r: ExecResult) => void,
    { kill = true }: { kill?: boolean } = {},
  ): NodeJS.Timeout {
    const timer = setTimeout(() => {
      if (kill) child.kill();
      done({ outcome: "failed", detail: "タイムアウト" });
    }, opts.timeoutMs);
    // タイマーがプロセス終了を引き止めないようにする（テストの終了も速くなる）。
    timer.unref?.();
    return timer;
  }

  // 同時に走らせる操作は 1 つまで。人が声で出す指示は元々直列だが、
  // STT の誤爆でループした場合やトークンを持つ相手の連打で、
  // PowerShell（数十 MB）やアプリが**無制限に増える**のを防ぐ。
  let inFlight = false;

  return {
    async execute(call: ToolCall): Promise<ExecResult> {
      if (call.name === "reply_only") return { outcome: "ok", detail: "" };
      if (inFlight) {
        return { outcome: "busy", detail: "別の操作を実行中" };
      }

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

      // ここから先が実際に子プロセスを作る区間。**必ず finally で解放する**
      // （解放し忘れると 1 回の失敗で操作機能が永久に閉じる）。
      inFlight = true;
      try {
        if (call.name === "launch_app") {
          return await launch(exePath);
        }
        const action = windowActionOf(
          call.name,
          call.name === "window_state" ? call.state : undefined,
        );
        const args = windowScriptArgs(opts.scriptPath, exePath, action);
        return await runWindowScript(args);
      } catch (err) {
        return { outcome: "denied", detail: safeLabel((err as Error).message) };
      } finally {
        inFlight = false;
      }
    },
  };
}
