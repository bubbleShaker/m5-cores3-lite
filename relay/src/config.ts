// 環境変数の解釈（#219）。**純粋関数だけ**を置く。
//
// なぜ切り出すか: 環境変数の読み出しを server.ts に直接書くと、
// 「既定が実行側に反転した」「タイムアウトが 0 になった」といった**この機能の生命線が
// 壊れてもテストが全部緑のまま通る**状態になる。ここに置けば表で固定できる。
import { DEFAULT_TIMEOUT_MS } from "./executor";

// 実際に PC を操作するかどうか。**未設定なら必ず dry-run（操作しない）**。
// 声で PC が動くのは戻せない副作用なので、"0" という明示的な opt-in だけを実行扱いにする。
export function isDryRun(env: { RELAY_DRY_RUN?: string }): boolean {
  return (env.RELAY_DRY_RUN ?? "1") !== "0";
}

// setTimeout が扱える上限（符号付き 32bit）。これを超えると Node は
// TimeoutOverflowWarning を出して **1ms に丸める**（＝全操作が spawn 直後に kill される）。
const MAX_TIMEOUT_MS = 2_147_483_647;

// ウィンドウ操作（PowerShell）を諦めるまでのミリ秒。
// 不正な値は既定へ落とす。空文字は 0、非数値は NaN、巨大値は 1ms 丸めと、
// **どれも「全操作が即タイムアウト」という同じ症状**を別の入口から引き起こす。
export function execTimeoutMs(env: { RELAY_EXEC_TIMEOUT_MS?: string }): {
  value: number;
  invalid: boolean;
} {
  const raw = env.RELAY_EXEC_TIMEOUT_MS;
  if (raw === undefined || raw.trim() === "") {
    return { value: DEFAULT_TIMEOUT_MS, invalid: false };
  }
  const value = Number(raw);
  if (!Number.isFinite(value) || value <= 0 || value > MAX_TIMEOUT_MS) {
    return { value: DEFAULT_TIMEOUT_MS, invalid: true };
  }
  return { value, invalid: false };
}
