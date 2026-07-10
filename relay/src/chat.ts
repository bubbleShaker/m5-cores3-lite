// 中継サーバの「純粋ロジック」。ネットワーク非依存なので native 同様に単体テストできる。
// 役割は2つ: (1) Claude へ渡すプロンプト組み立て (2) Claude 応答のパース＆検証。

// アバター側(face_logic.h)の表情語彙と一致させる。ここがクラウドとデバイスの「契約」になる。
export type Expression = "neutral" | "happy" | "thinking" | "sad" | "surprised";
export type Action = "none" | "notify";

export interface ChatReply {
  reply: string;
  expression: Expression;
  action: Action;
}

const VALID_EXPRESSIONS: readonly Expression[] = [
  "neutral",
  "happy",
  "thinking",
  "sad",
  "surprised",
];
const VALID_ACTIONS: readonly Action[] = ["none", "notify"];

// Claude が語彙外の値を返しても安全側に倒す（不正 → neutral / none）。
export function normalizeExpression(value: unknown): Expression {
  if (typeof value === "string") {
    const v = value.trim().toLowerCase();
    if ((VALID_EXPRESSIONS as readonly string[]).includes(v)) {
      return v as Expression;
    }
  }
  return "neutral";
}

export function normalizeAction(value: unknown): Action {
  if (typeof value === "string") {
    const v = value.trim().toLowerCase();
    if ((VALID_ACTIONS as readonly string[]).includes(v)) {
      return v as Action;
    }
  }
  return "none";
}

// Claude へ「JSON だけを返せ」と指示する system プロンプト。
// 構造化出力を強制することで、デバイス側のパースを単純に保つ。
export const SYSTEM_PROMPT = [
  "あなたは M5Stack の小さな画面に住むドット絵アバターです。",
  "親しみやすく簡潔に日本語で応答してください。reply は2文以内・全角40文字程度まで。",
  "出力は必ず次の形の JSON のみ。前後に説明やコードフェンスを付けないこと。",
  '{"reply": "<返答文>", "expression": "<neutral|happy|thinking|sad|surprised>", "action": "<none|notify>"}',
  "expression は返答の感情に最も近いものを選ぶ。action は通知すべき要件があれば notify、無ければ none。",
].join("\n");

// Claude SDK の messages 配列を組み立てる（ユーザー発話1件）。
export function buildMessages(userMessage: string): { role: "user"; content: string }[] {
  return [{ role: "user", content: userMessage }];
}

// テキストから最初の { ～ 最後の } を切り出す。
// Claude がうっかり前後に文を付けても JSON 本体を拾えるようにする保険。
function extractJson(text: string): string {
  const start = text.indexOf("{");
  const end = text.lastIndexOf("}");
  if (start >= 0 && end > start) {
    return text.slice(start, end + 1);
  }
  return text;
}

// Claude の応答テキストを ChatReply に変換。失敗時もデバイスが死なないよう必ず妥当な値を返す。
export function parseClaudeReply(text: string): ChatReply {
  let parsed: unknown;
  try {
    parsed = JSON.parse(extractJson(text));
  } catch {
    // JSON にできなければテキスト全体を reply として扱う（フォールバック）。
    return { reply: text.trim(), expression: "neutral", action: "none" };
  }
  const obj = (parsed ?? {}) as Record<string, unknown>;
  return {
    reply: typeof obj.reply === "string" ? obj.reply : "",
    expression: normalizeExpression(obj.expression),
    action: normalizeAction(obj.action),
  };
}
