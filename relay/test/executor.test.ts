// 実行アダプタのテスト（#219）。**実際にアプリは 1 つも起動しない**。
// spawn を差し替えて「何を・どんな引数で起動しようとしたか」だけを検証する。
import { describe, it, expect, beforeEach } from "vitest";
import { createExecutor, type SpawnFn, type SpawnedProcess } from "../src/executor";

// spawn された子プロセスの偽物。イベントは呼び出し側の listener 登録後に発火させる。
class FakeChild implements SpawnedProcess {
  private listeners = new Map<string, ((...args: never[]) => void)[]>();
  unrefed = false;
  killed = false;

  on(event: string, listener: (...args: never[]) => void) {
    const list = this.listeners.get(event) ?? [];
    list.push(listener);
    this.listeners.set(event, list);
    return this;
  }
  unref() {
    this.unrefed = true;
  }
  kill() {
    this.killed = true;
    return true;
  }
  emit(event: string, ...args: unknown[]) {
    for (const listener of this.listeners.get(event) ?? []) {
      (listener as (...a: unknown[]) => void)(...args);
    }
  }
}

interface SpawnCall {
  command: string;
  args: string[];
  options: Record<string, unknown>;
}

let calls: SpawnCall[] = [];
let behavior: (child: FakeChild) => void = (child) => child.emit("spawn");

const spawn: SpawnFn = (command, args, options) => {
  calls.push({ command, args: [...args], options: { ...options } });
  const child = new FakeChild();
  // listener の登録は spawn の**戻り値**に対して行われるので、発火は次のマイクロタスクへ回す。
  queueMicrotask(() => behavior(child));
  return child;
};

const APPS = {
  browser: "C:/Program Files/Edge/msedge.exe",
  editor: "C:/Users/x/Code.exe",
  // 起動時検証を通らない値。executor が実行直前に再検証することの確認用。
  broken: "C:/apps/../Windows/System32/cmd.exe",
};

const make = (timeoutMs = 50) =>
  createExecutor(APPS, { spawn, scriptPath: "C:\\relay\\window.ps1", timeoutMs });

beforeEach(() => {
  calls = [];
  behavior = (child) => child.emit("spawn");
});

describe("launch_app", () => {
  it("apps.json のパスを引数なしで起動する", async () => {
    const result = await make().execute({ name: "launch_app", app: "browser" });
    expect(result.outcome).toBe("ok");
    expect(calls).toHaveLength(1);
    expect(calls[0].command).toBe("C:\\Program Files\\Edge\\msedge.exe");
    // ⚠ ここが空でなくなったら、LLM から引数を流し込む口が開いたということ。
    expect(calls[0].args).toEqual([]);
    expect(calls[0].options.shell).toBe(false);
    expect(calls[0].options.detached).toBe(true);
  });

  it("spawn の error イベントを例外にせず failed へ写像する", async () => {
    behavior = (child) => child.emit("error", new Error("spawn ENOENT"));
    const result = await make().execute({ name: "launch_app", app: "browser" });
    expect(result.outcome).toBe("failed");
    expect(result.detail).toContain("ENOENT");
  });

  it("spawn が同期的に投げても落ちない", async () => {
    const throwing: SpawnFn = () => {
      throw new Error("EPERM");
    };
    const executor = createExecutor(APPS, { spawn: throwing });
    const result = await executor.execute({ name: "launch_app", app: "browser" });
    expect(result.outcome).toBe("failed");
  });
});

describe("許可リスト", () => {
  it("apps.json に無いキーは spawn を呼ばずに拒否する", async () => {
    // 型の上では通らない値でも、実行層は自前で弾く（tools.ts の改変で穴が開かないように）。
    const result = await make().execute({ name: "launch_app", app: "cmd" });
    expect(result.outcome).toBe("denied");
    expect(calls).toHaveLength(0);
  });

  it("プロトタイプ由来のキーを登録済みとみなさない", async () => {
    for (const app of ["constructor", "toString", "__proto__"]) {
      const result = await make().execute({ name: "launch_app", app });
      expect(result.outcome).toBe("denied");
    }
    expect(calls).toHaveLength(0);
  });

  it("登録済みでもパスが不正なら実行しない（実行直前の再検証）", async () => {
    const result = await make().execute({ name: "launch_app", app: "broken" });
    expect(result.outcome).toBe("denied");
    expect(calls).toHaveLength(0);
  });
});

describe("ウィンドウ操作", () => {
  it("window.ps1 を -File で呼ぶ（プロセス名と操作はパラメータで渡す）", async () => {
    behavior = (child) => child.emit("exit", 0);
    const result = await make().execute({
      name: "window_state",
      app: "browser",
      state: "minimize",
    });
    expect(result.outcome).toBe("ok");
    expect(calls[0].command).toBe("powershell.exe");
    expect(calls[0].args).toEqual([
      "-NoProfile",
      "-NonInteractive",
      "-ExecutionPolicy",
      "Bypass",
      "-File",
      "C:\\relay\\window.ps1",
      "-ProcessName",
      "msedge",
      "-Action",
      "minimize",
    ]);
    expect(calls[0].options.shell).toBe(false);
  });

  it("focus_window は -Action focus になる", async () => {
    behavior = (child) => child.emit("exit", 0);
    const result = await make().execute({ name: "focus_window", app: "editor" });
    expect(result.outcome).toBe("ok");
    expect(calls[0].args).toContain("Code");
    expect(calls[0].args.at(-1)).toBe("focus");
  });

  it("終了コード 3 は not_running（窓が無いだけ・失敗ではない）", async () => {
    behavior = (child) => child.emit("exit", 3);
    const result = await make().execute({ name: "focus_window", app: "browser" });
    expect(result.outcome).toBe("not_running");
  });

  it("その他の終了コードは failed", async () => {
    behavior = (child) => child.emit("exit", 1);
    const result = await make().execute({ name: "focus_window", app: "browser" });
    expect(result.outcome).toBe("failed");
  });

  it("無応答なら kill してタイムアウト扱いにする", async () => {
    let spawned: FakeChild | undefined;
    behavior = (child) => {
      spawned = child; // 何も emit しない＝返ってこない PowerShell
    };
    const result = await make(10).execute({ name: "focus_window", app: "browser" });
    expect(result.outcome).toBe("failed");
    expect(result.detail).toContain("タイムアウト");
    expect(spawned?.killed).toBe(true);
  });
});

describe("reply_only", () => {
  it("何も起動しない", async () => {
    const result = await make().execute({ name: "reply_only" });
    expect(result.outcome).toBe("ok");
    expect(calls).toHaveLength(0);
  });
});
