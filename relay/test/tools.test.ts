import { describe, it, expect } from "vitest";
import {
  buildToolsPrompt,
  EMPTY_APPS,
  parseAppsConfig,
  parseToolCall,
  TOOL_NAMES,
  toolSpeech,
  WINDOW_STATES,
  type ToolCall,
} from "../src/tools";

// 検証で使う許可リスト。実運用では apps.json のキーがここに来る。
const APPS = ["browser", "editor", "terminal"] as const;

describe("parseAppsConfig", () => {
  it("キー→パスの対応を読み取る", () => {
    const cfg = parseAppsConfig('{"browser":"C:/x/msedge.exe","editor":"C:/y/code.exe"}');
    expect(cfg.keys).toEqual(["browser", "editor"]);
    expect(cfg.apps.browser).toBe("C:/x/msedge.exe");
  });

  it("パスの前後空白を落とす", () => {
    expect(parseAppsConfig('{"browser":"  C:/x.exe  "}').apps.browser).toBe("C:/x.exe");
  });

  it("空の設定も有効（操作はできないが対話はできる）", () => {
    expect(parseAppsConfig("{}")).toEqual({ apps: {}, keys: [] });
  });

  it("JSON として壊れていれば投げる", () => {
    expect(() => parseAppsConfig("{ not json")).toThrow(/valid JSON/);
  });

  it("オブジェクト以外は投げる（配列・null・数値）", () => {
    expect(() => parseAppsConfig("[]")).toThrow(/JSON object/);
    expect(() => parseAppsConfig("null")).toThrow(/JSON object/);
    expect(() => parseAppsConfig("42")).toThrow(/JSON object/);
  });

  it("キーの形が不正なら投げる（大文字・空白・記号・長すぎ）", () => {
    for (const bad of ["Browser", "web browser", "app!", "a".repeat(33), "", "-lead"]) {
      expect(() => parseAppsConfig(JSON.stringify({ [bad]: "C:/x.exe" }))).toThrow(/invalid key/);
    }
  });

  it("値が文字列でない・空ならば投げる", () => {
    expect(() => parseAppsConfig('{"browser":123}')).toThrow(/non-empty string/);
    expect(() => parseAppsConfig('{"browser":""}')).toThrow(/non-empty string/);
    expect(() => parseAppsConfig('{"browser":"   "}')).toThrow(/non-empty string/);
    expect(() => parseAppsConfig('{"browser":null}')).toThrow(/non-empty string/);
  });

  it("パスが長すぎれば投げる", () => {
    expect(() => parseAppsConfig(JSON.stringify({ browser: "C:/" + "a".repeat(600) }))).toThrow(/too long/);
  });

  it("エントリ数が多すぎれば投げる（プロンプトへ連結されるため）", () => {
    const many: Record<string, string> = {};
    for (let i = 0; i < 33; i++) many[`app${i}`] = "C:/x.exe";
    expect(() => parseAppsConfig(JSON.stringify(many))).toThrow(/too many/);
  });

  it("パスに制御文字が混ざっていれば投げる（#219 で実行される値のため）", () => {
    expect(() => parseAppsConfig('{"browser":"C:/a\\nb.exe"}')).toThrow(/control characters/);
    expect(() => parseAppsConfig('{"browser":"C:/a\\u2028b.exe"}')).toThrow(/control characters/);
  });

  it("__proto__ をキーにしても弾かれ、Object.prototype も汚れない", () => {
    expect(() => parseAppsConfig('{"__proto__":"C:/evil.exe"}')).toThrow(/invalid key/);
    expect(({} as Record<string, unknown>).polluted).toBeUndefined();
    expect(Object.prototype.hasOwnProperty.call(Object.prototype, "__proto__x")).toBe(false);
  });

  it("継承プロパティ名は「登録済み」に見えない", () => {
    const cfg = parseAppsConfig('{"browser":"C:/x.exe"}');
    // Object.create(null) 由来なので constructor / toString は生えていない。
    expect(cfg.apps.constructor).toBeUndefined();
    expect((cfg.apps as Record<string, unknown>).toString).toBeUndefined();
    expect(cfg.keys).toEqual(["browser"]);
  });

  it("エラーメッセージに改行を持ち込ませない（ログ偽装対策）", () => {
    let message = "";
    try {
      parseAppsConfig(JSON.stringify({ "bad\nkey": "C:/x.exe" }));
    } catch (e) {
      message = (e as Error).message;
    }
    expect(message).toContain("invalid key");
    expect(message).not.toContain("\n");
  });
});

describe("parseToolCall — 正常系", () => {
  it("tool が無ければ reply_only（拒否ではない）", () => {
    expect(parseToolCall(undefined, APPS)).toEqual({ call: { name: "reply_only" }, rejected: null });
    expect(parseToolCall(null, APPS)).toEqual({ call: { name: "reply_only" }, rejected: null });
  });

  it("reply_only をそのまま受理する", () => {
    expect(parseToolCall({ name: "reply_only" }, APPS).call).toEqual({ name: "reply_only" });
  });

  it("launch_app を受理する", () => {
    const r = parseToolCall({ name: "launch_app", args: { app: "browser" } }, APPS);
    expect(r).toEqual({ call: { name: "launch_app", app: "browser" }, rejected: null });
  });

  it("focus_window を受理する", () => {
    const r = parseToolCall({ name: "focus_window", args: { app: "editor" } }, APPS);
    expect(r.call).toEqual({ name: "focus_window", app: "editor" });
  });

  it.each(WINDOW_STATES)("window_state(%s) を受理する", (state) => {
    const r = parseToolCall({ name: "window_state", args: { app: "browser", state } }, APPS);
    expect(r).toEqual({ call: { name: "window_state", app: "browser", state }, rejected: null });
  });

  it("大文字・前後空白を正規化する", () => {
    const r = parseToolCall({ name: " LAUNCH_APP ", args: { app: " Browser " } }, APPS);
    expect(r.call).toEqual({ name: "launch_app", app: "browser" });
  });
});

describe("parseToolCall — 拒否（必ず reply_only に落ちる）", () => {
  const denied = (raw: unknown) => {
    const r = parseToolCall(raw, APPS);
    expect(r.call).toEqual({ name: "reply_only" });
    expect(r.rejected).toBeTruthy();
    return r.rejected as string;
  };

  it("許可リストに無いアプリは拒否する（任意の exe を走らせない担保）", () => {
    expect(denied({ name: "launch_app", args: { app: "cmd" } })).toMatch(/登録されていない/);
    expect(denied({ name: "launch_app", args: { app: "C:/windows/system32/cmd.exe" } })).toMatch(/登録されていない/);
  });

  it("未知のツール名は拒否する", () => {
    expect(denied({ name: "run_shell", args: { cmd: "rm -rf /" } })).toMatch(/未知のツール/);
    expect(denied({ name: "delete_file", args: { app: "browser" } })).toMatch(/未知のツール/);
  });

  it("object でない tool は拒否する", () => {
    for (const bad of ["launch_app", 42, true, ["launch_app"]]) denied(bad);
  });

  it("name が無い・文字列でないなら拒否する", () => {
    denied({});
    denied({ args: { app: "browser" } });
    denied({ name: 42 });
  });

  it("app が無い・空・文字列でないなら拒否する", () => {
    expect(denied({ name: "launch_app" })).toMatch(/args.app/);
    expect(denied({ name: "launch_app", args: {} })).toMatch(/args.app/);
    expect(denied({ name: "launch_app", args: { app: "" } })).toMatch(/args.app/);
    expect(denied({ name: "launch_app", args: { app: 1 } })).toMatch(/args.app/);
  });

  it("args が object でなければ空扱いになり、app 不足で拒否される", () => {
    expect(denied({ name: "launch_app", args: "browser" })).toMatch(/args.app/);
    expect(denied({ name: "launch_app", args: ["browser"] })).toMatch(/args.app/);
    expect(denied({ name: "launch_app", args: null })).toMatch(/args.app/);
  });

  it("window_state の state が無い・語彙外なら拒否する", () => {
    expect(denied({ name: "window_state", args: { app: "browser" } })).toMatch(/args.state/);
    expect(denied({ name: "window_state", args: { app: "browser", state: "close" } })).toMatch(/未知の state/);
    expect(denied({ name: "window_state", args: { app: "browser", state: "destroy" } })).toMatch(/未知の state/);
  });

  // launch_app 1 件だけで確かめると、「app を取らない新ツール」を足した瞬間に
  // 「許可リストが空＝安全」が崩れたことに気づけない。全ツールを回す。
  it.each(TOOL_NAMES)("許可リストが空なら %s も操作に到達しない", (name) => {
    const r = parseToolCall(
      { name, args: { app: "browser", state: "minimize" } },
      [],
    );
    expect(r.call).toEqual({ name: "reply_only" });
    if (name !== "reply_only") expect(r.rejected).toMatch(/登録されていない/);
  });

  it("拒否理由に改行を持ち込ませない（ログ偽装対策）", () => {
    const reason = denied({ name: "evil\ntool: ok", args: { app: "browser" } });
    expect(reason).not.toContain("\n");
  });

  it("拒否理由は長さを制限する", () => {
    const reason = denied({ name: "x".repeat(500) });
    expect(reason.length).toBeLessThan(80);
  });
});

describe("parseToolCall — 余計なキーを持ち込まない", () => {
  it("入力の追加フィールドは結果に混ざらない", () => {
    const r = parseToolCall(
      { name: "launch_app", args: { app: "browser", extra: "rm -rf /" }, command: "evil" },
      APPS,
    );
    expect(r.call).toEqual({ name: "launch_app", app: "browser" });
    expect(Object.keys(r.call)).toEqual(["name", "app"]);
  });

  it("prototype 汚染を狙うキーを持ち込まない", () => {
    const r = parseToolCall(
      JSON.parse('{"name":"launch_app","args":{"app":"browser","__proto__":{"polluted":true}}}'),
      APPS,
    );
    expect(r.call).toEqual({ name: "launch_app", app: "browser" });
    expect(({} as Record<string, unknown>).polluted).toBeUndefined();
  });
});

describe("buildToolsPrompt", () => {
  it("許可されたアプリ名を列挙する", () => {
    const p = buildToolsPrompt(APPS);
    for (const app of APPS) expect(p).toContain(app);
    expect(p).toContain("launch_app");
    expect(p).toContain("window_state");
  });

  it("許可アプリが空なら操作できないと明示し、道具の説明を出さない", () => {
    const p = buildToolsPrompt(EMPTY_APPS.keys);
    expect(p).toContain("reply_only");
    expect(p).not.toContain("launch_app");
  });

  it("一覧外は実行できないと明示する（幻覚の抑止）", () => {
    expect(buildToolsPrompt(APPS)).toMatch(/一覧に無い/);
  });
});

describe("toolSpeech", () => {
  const call: ToolCall = { name: "launch_app", app: "browser" };

  it("reply_only は喋る文を持たない（LLM の reply をそのまま使う）", () => {
    expect(toolSpeech({ name: "reply_only" }, "ok")).toBe("");
  });

  it("成功したら何をしたか言う", () => {
    expect(toolSpeech(call, "ok")).toContain("browser");
    expect(toolSpeech({ name: "focus_window", app: "editor" }, "ok")).toContain("切り替え");
    expect(toolSpeech({ name: "window_state", app: "browser", state: "minimize" }, "ok")).toContain("最小化");
  });

  it("拒否・失敗は成功と区別できる（成功したと偽らない）", () => {
    expect(toolSpeech(call, "denied")).not.toContain("開いた");
    expect(toolSpeech(call, "failed")).not.toContain("開いた");
    expect(toolSpeech(call, "denied")).not.toBe(toolSpeech(call, "failed"));
  });

  // 拒否された要求は parseToolCall が reply_only に落とすので、
  // 「reply_only は無言」を先に判定すると拒否を伝えられなくなる（順序が効いている）。
  it("reply_only に落ちた拒否でも「できない」と言える", () => {
    expect(toolSpeech({ name: "reply_only" }, "denied")).toContain("できない");
    expect(toolSpeech({ name: "reply_only" }, "failed")).not.toBe("");
  });
});

describe("ツール定義そのもの", () => {
  it("実行できる操作は 3 つ + reply_only だけ（増減は意図的な変更のはず）", () => {
    expect(TOOL_NAMES).toEqual(["reply_only", "launch_app", "focus_window", "window_state"]);
  });

  // ツールを 1 つ足すと TOOL_NAMES / parseToolCall / buildToolsPrompt / toolSpeech の
  // 4 箇所を同期させる必要がある。文字列側の漏れは型では検出できないのでテストで縛る。
  it.each(TOOL_NAMES.filter((n) => n !== "reply_only"))(
    "%s はプロンプトにも説明が載っている",
    (name) => {
      expect(buildToolsPrompt(APPS)).toContain(name);
    },
  );

  it("すべてのツールが喋る文を持つ（未定義の穴が無い）", () => {
    const calls: ToolCall[] = [
      { name: "launch_app", app: "browser" },
      { name: "focus_window", app: "browser" },
      { name: "window_state", app: "browser", state: "minimize" },
    ];
    for (const call of calls) {
      expect(toolSpeech(call, "ok")).not.toBe("");
      expect(toolSpeech(call, "ok")).not.toBeUndefined();
    }
  });

  it("破壊的な操作は定義に含まれない", () => {
    const names = TOOL_NAMES.join(" ");
    for (const dangerous of ["shell", "exec", "delete", "remove", "shutdown", "kill", "write"]) {
      expect(names).not.toContain(dangerous);
    }
  });
});
