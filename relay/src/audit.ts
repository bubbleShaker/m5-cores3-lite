// 監査ログの整形（#219）。純粋関数なので vitest で検証できる。
//
// なぜ要るか: 声で PC が動く以上、**いつ・どの発話から・どの操作を・どうなったか**が
// 後から辿れないと、誤作動と「言った覚えのない操作」を切り分けられない。
// 1 行 1 レコードの JSON にしてあるのは、後で grep / jq で追えるようにするため。
import { safeLabel, type ToolCall } from "./tools";

// 実行層の結果に加えて、実行しなかった場合（dry-run / 拒否）も同じ形で残す。
export type AuditOutcome = "ok" | "denied" | "failed" | "not_running" | "dry-run";

export interface AuditInput {
  // ISO8601。呼び出し側から渡す（この関数を時刻に依存させないため＝テストが安定する）。
  time: string;
  // ユーザーの発話（STT の結果）。LLM ではなく人間由来だが、同じく信用しない。
  utterance: string;
  call: ToolCall;
  outcome: AuditOutcome;
  detail?: string;
}

export function formatAudit(input: AuditInput): string {
  const { call } = input;
  // ⚠ 発話も detail も外部由来。改行を残すと 1 行 1 レコードの前提が崩れ、
  // 偽のログ行を注入できてしまう（ログインジェクション）。safeLabel で潰す。
  const record: Record<string, string> = {
    time: input.time,
    utterance: safeLabel(input.utterance, 120),
    tool: call.name,
    outcome: input.outcome,
  };
  if (call.name !== "reply_only") record.app = call.app;
  if (call.name === "window_state") record.state = call.state;
  if (input.detail) record.detail = safeLabel(input.detail, 120);
  // JSON.stringify がクォートとエスケープを担うので、ここでも文字列連結で組み立てない。
  return `audit ${JSON.stringify(record)}`;
}
