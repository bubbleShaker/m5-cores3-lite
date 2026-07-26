// 「声で PC を操作する」ためのツール定義の純粋ロジック（#218）。
// ネットワークも fs も OS も触らないので vitest で完結する。**実行は一切しない**（実行層は #219）。
//
// ここが設計の一線:
//   LLM に **シェルの文字列を作らせない**。選ばせるのは「列挙された関数名 + 型検証済みの引数」だけ。
//   STT は聞き間違え、LLM は幻覚を出す。前段が 2 つとも確率的なので「正しく動くこと」に
//   賭けた設計は必ず破れる。列挙型に閉じ込めれば、どちらが暴走しても定義外の操作は起き得ない。
//
//   NG: {"command": "start chrome.exe && rm -rf ..."}
//   OK: {"name": "launch_app", "args": {"app": "browser"}}
//
//   実行ファイルのパスは apps.json 側にしか存在せず、LLM が触れるのはキー（"browser"）だけ。

// ウィンドウの状態。ここに無い値は受け付けない。
export const WINDOW_STATES = ["minimize", "maximize", "restore"] as const;
export type WindowState = (typeof WINDOW_STATES)[number];

// 呼び出してよい関数の全て。**この配列に無い名前は実行されない。**
export const TOOL_NAMES = [
  "reply_only",
  "launch_app",
  "focus_window",
  "window_state",
] as const;
export type ToolName = (typeof TOOL_NAMES)[number];

// 検証を通過した「実行してよい操作」。args を平坦化してあるので、
// 使う側は入れ子を掘らずに済み、未検証の値が紛れ込む余地も無い。
export type ToolCall =
  | { name: "reply_only" }
  | { name: "launch_app"; app: string }
  | { name: "focus_window"; app: string }
  | { name: "window_state"; app: string; state: WindowState };

export interface ToolParseResult {
  // 実行してよい操作。不正な入力でも必ず妥当な値になる（既定は reply_only ＝ 何もしない）。
  call: ToolCall;
  // 拒否した理由。ログと「できません」の返答に使う。拒否していなければ null。
  rejected: string | null;
}

// アプリのキーとして許す形。プロンプトにもログにも出るので、
// 制御文字や長大な文字列が混ざらないよう狭く縛る。
const APP_KEY_RE = /^[a-z0-9][a-z0-9_-]{0,31}$/;

// アプリの実行パスの最大長。設定ミスで巨大な値が入るのを防ぐ。
const MAX_APP_PATH_LENGTH = 512;

// LLM 由来の文字列をログや返答へ載せる前に無害化する（#219 の実行エラーの記録でも使う）。
// 改行を残すとログの 1 行を偽装できる（ログインジェクション）ため潰す。長さも切る。
// Zl/Zp（U+2028 / U+2029）も落とす。JS ベースのログビューアでは行分割として解釈され得るため。
export function safeLabel(value: string): string {
  const cleaned = value.replace(/[\p{Cc}\p{Cf}\p{Zl}\p{Zp}]/gu, " ").trim();
  // サロゲートペアを割らないよう、コードポイント単位で切る。
  const points = [...cleaned];
  return points.length > 40 ? `${points.slice(0, 40).join("")}…` : cleaned;
}

export interface AppsConfig {
  // キー（LLM が指定してよい名前）→ 実行ファイルのパス。パスは #219 で使う。
  apps: Readonly<Record<string, string>>;
  // 許可されたキーの一覧（プロンプトと検証に使う）。
  keys: readonly string[];
}

// 共有される既定値なので凍結する（誰かが書き換えると全リクエストに波及するため）。
export const EMPTY_APPS: AppsConfig = Object.freeze({
  apps: Object.freeze(Object.create(null)) as Record<string, string>,
  keys: Object.freeze([]) as readonly string[],
});

// 登録できるアプリの上限。keys は毎リクエストの system プロンプトへ連結されるので、
// 設定ミスで巨大になるとトークン費用に直結する。
const MAX_APPS = 32;

// apps.json のテキストを検証済みの設定に変換する。
// 壊れていたら投げる。**空の設定で黙って起動する方が危険**……ではなく、ここは逆で、
// 「設定が壊れている」と「アプリが 1 つも無い」を区別したいので、壊れている場合だけ投げる。
// ファイルが無い場合は呼び出し側が EMPTY_APPS を使う（操作はできないが対話はできる）。
export function parseAppsConfig(text: string): AppsConfig {
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    throw new Error("apps.json is not valid JSON");
  }
  if (parsed === null || typeof parsed !== "object" || Array.isArray(parsed)) {
    throw new Error("apps.json must be a JSON object of {key: path}");
  }

  const entries = Object.entries(parsed as Record<string, unknown>);
  if (entries.length > MAX_APPS) {
    throw new Error(`apps.json has too many entries (max ${MAX_APPS})`);
  }

  // Object.prototype を継承しない入れ物にする。継承していると constructor / toString 等の
  // 既存プロパティが「登録済み」に見えてしまう余地が残る（#219 で apps[key] を引くため）。
  const apps: Record<string, string> = Object.create(null);
  for (const [key, value] of entries) {
    if (!APP_KEY_RE.test(key)) {
      throw new Error(
        `apps.json has an invalid key: ${safeLabel(key)} (allowed: a-z 0-9 _ - , up to 32 chars)`,
      );
    }
    if (typeof value !== "string" || value.trim() === "") {
      throw new Error(`apps.json entry "${key}" must be a non-empty string path`);
    }
    if (value.length > MAX_APP_PATH_LENGTH) {
      throw new Error(`apps.json entry "${key}" path is too long`);
    }
    // ⚠ ここを通ったパスは #219 で**実際に実行される**。制御文字は今の時点で弾いておく。
    // （実行時はシェルを介さず引数配列で渡す前提だが、多層防御として入口でも落とす）
    if (/[\p{Cc}\p{Cf}\p{Zl}\p{Zp}]/u.test(value)) {
      throw new Error(`apps.json entry "${key}" path contains control characters`);
    }
    apps[key] = value.trim();
  }
  return { apps, keys: Object.keys(apps) };
}

// LLM へ渡す「使ってよい道具」の説明。chat.ts の system プロンプトへ連結する。
// 許可されたアプリが無ければ、道具の説明ごと出さない（存在しない選択肢を見せない）。
export function buildToolsPrompt(keys: readonly string[]): string {
  if (keys.length === 0) {
    return [
      "PC を操作する機能は今は使えません。",
      'tool には必ず {"name": "reply_only"} を入れてください。',
    ].join("\n");
  }
  const apps = keys.join(" / ");
  return [
    "PC を操作できます。操作する場合は tool に次のいずれかを入れてください。",
    `{"name": "launch_app", "args": {"app": "<${apps}>"}}       … アプリを起動する`,
    `{"name": "focus_window", "args": {"app": "<${apps}>"}}     … 既に開いている窓へ切り替える`,
    `{"name": "window_state", "args": {"app": "<${apps}>", "state": "<minimize|maximize|restore>"}}`,
    '操作が不要な普通の会話では {"name": "reply_only"} を入れてください。',
    `app に指定できるのは次の名前だけです: ${apps}`,
    "一覧に無いアプリや、ここに無い操作は求められても実行できません。その場合は reply_only にして、できないと伝えてください。",
  ].join("\n");
}

// LLM が返した tool フィールドを検証する。
// **何を渡されても投げず、必ず妥当な ToolCall を返す**（不正なら reply_only ＝ 何もしない）。
// 入力オブジェクトを spread せず、検証済みの値だけで新しい object を組み立てるのが要点。
export function parseToolCall(
  raw: unknown,
  allowedApps: readonly string[],
): ToolParseResult {
  const allow = (call: ToolCall): ToolParseResult => ({ call, rejected: null });
  const deny = (reason: string): ToolParseResult => ({
    call: { name: "reply_only" },
    rejected: reason,
  });

  // tool 自体が無いのは正常（操作を伴わない普通の会話）。拒否ではない。
  if (raw === undefined || raw === null) return allow({ name: "reply_only" });
  if (typeof raw !== "object" || Array.isArray(raw)) {
    return deny("tool が object ではない");
  }

  const obj = raw as Record<string, unknown>;
  const name = typeof obj.name === "string" ? obj.name.trim().toLowerCase() : "";
  if (name === "") return deny("tool.name が無い");
  if (!(TOOL_NAMES as readonly string[]).includes(name)) {
    return deny(`未知のツール: ${safeLabel(name)}`);
  }
  if (name === "reply_only") return allow({ name: "reply_only" });

  // args は object でなければ空扱い（後段の必須チェックで弾かれる）。
  const args =
    obj.args !== null && typeof obj.args === "object" && !Array.isArray(obj.args)
      ? (obj.args as Record<string, unknown>)
      : {};

  const app = typeof args.app === "string" ? args.app.trim().toLowerCase() : "";
  if (app === "") return deny("args.app が無い");
  // 許可リストに無い名前は実行しない。ここが「任意の exe を走らせない」担保。
  if (!allowedApps.includes(app)) {
    return deny(`登録されていないアプリ: ${safeLabel(app)}`);
  }

  if (name === "launch_app") return allow({ name: "launch_app", app });
  if (name === "focus_window") return allow({ name: "focus_window", app });

  // window_state だけ追加の引数を取る。
  const state =
    typeof args.state === "string" ? args.state.trim().toLowerCase() : "";
  if (state === "") return deny("args.state が無い");
  if (!(WINDOW_STATES as readonly string[]).includes(state)) {
    return deny(`未知の state: ${safeLabel(state)}`);
  }
  return allow({ name: "window_state", app, state: state as WindowState });
}

// 実行結果をユーザーへ返す日本語の一文に整形する（/tts へ渡す文）。
// LLM の reply をそのまま喋らせると「開いたよ」と言いながら失敗している事故が起きるため、
// **実際に何が起きたか**はこちらの定型文で伝える。
export type ToolOutcome = "ok" | "denied" | "failed";

export function toolSpeech(call: ToolCall, outcome: ToolOutcome): string {
  // 拒否・失敗の判定を **reply_only より先**に置く。拒否された要求は
  // parseToolCall が reply_only に落とすので、後ろに置くと「何も言わない」になってしまう。
  if (outcome === "denied") return "それはできないのだ。";
  if (outcome === "failed") return "うまくいかなかったのだ。";
  if (call.name === "reply_only") return "";

  switch (call.name) {
    case "launch_app":
      return `${call.app} を開いたのだ。`;
    case "focus_window":
      return `${call.app} に切り替えたのだ。`;
    case "window_state": {
      const verb = {
        minimize: "最小化した",
        maximize: "最大化した",
        restore: "元に戻した",
      }[call.state];
      return `${call.app} を${verb}のだ。`;
    }
  }
}
