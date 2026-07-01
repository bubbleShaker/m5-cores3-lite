// 外部変換コマンド（FFmpeg 等）を子プロセスで実行する副作用アダプタ。
// server.ts の import 時副作用（serve）から切り離し、env にも依存しない形で受け取ることで
// フェイクコマンドを注入した単体テストを可能にする（child_process の分岐が最もリスクが高いため）。
//
// 「入力バイト列を stdin へ流し、stdout の出力を回収する」だけの汎用処理。
// タイムアウト・出力サイズ上限・二重解決防止（ゾンビ/ハング対策）をここに集約する。
import { spawn } from "node:child_process";

export interface TranscodeOptions {
  bin: string; // 実行ファイル（PATH 解決）。
  args: string[]; // 引数配列（shell=false。ユーザー入力を含めないこと）。
  maxOutputBytes: number; // stdout 出力の上限。超過で SIGKILL して拒否。
  timeoutMs: number; // 無応答を打ち切る上限。
  stderrCap?: number; // 診断用 stderr の蓄積上限（既定 4096）。
}

// 子プロセスを起動し、input を stdin へ、stdout を回収して返す。
// 失敗（非ゼロ終了 / spawn 失敗 / タイムアウト / サイズ超過 / stdin エラー）は必ず reject する。
export function runTranscode(
  input: Uint8Array,
  opts: TranscodeOptions,
): Promise<Uint8Array> {
  const stderrCap = opts.stderrCap ?? 4096;
  return new Promise((resolve, reject) => {
    const proc = spawn(opts.bin, opts.args);
    const chunks: Buffer[] = [];
    let total = 0;
    let stderr = "";

    // 全ハンドラ（stdout超過 / close / error / stdin error / timeout）が参照する単一の完了フラグ。
    // 二重 resolve/reject と、解決後の後始末漏れ（ゾンビ・タイマー残り）を防ぐ。
    let settled = false;
    const finish = (fn: () => void) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      if (!proc.killed) proc.kill("SIGKILL"); // 未終了なら確実に殺す（ハング・超過時）。
      fn();
    };

    // 無応答の子プロセスを一定時間で打ち切る（ゾンビ化防止の要）。
    const timer = setTimeout(() => {
      finish(() => reject(new Error("transcode timed out")));
    }, opts.timeoutMs);

    proc.stdout.on("data", (chunk: Buffer) => {
      total += chunk.length;
      if (total > opts.maxOutputBytes) {
        finish(() => reject(new Error("transcode output exceeds size limit")));
        return;
      }
      chunks.push(chunk);
    });

    // stderr は診断用に上限付きで握る（異常時の無制限連結を防ぐ）。
    proc.stderr.on("data", (d: Buffer) => {
      if (stderr.length < stderrCap) {
        stderr += d.toString().slice(0, stderrCap - stderr.length);
      }
    });

    proc.on("error", (err) => finish(() => reject(err))); // spawn 自体の失敗（コマンド不在等）。
    proc.on("close", (code) => {
      finish(() => {
        if (code !== 0) {
          reject(new Error(`process exited ${code}: ${stderr.trim()}`));
        } else {
          resolve(new Uint8Array(Buffer.concat(chunks)));
        }
      });
    });

    // 入力を stdin へ流し込む。書き込みエラー（EPIPE 等）も拾う。
    proc.stdin.on("error", (err) => finish(() => reject(err)));
    proc.stdin.end(input);
  });
}
