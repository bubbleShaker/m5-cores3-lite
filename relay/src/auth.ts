// 中継サーバの認証の「純粋ロジック」。node:crypto 以外に依存しないので vitest で完結する。
// 役割は3つ: (1) 起動時のトークン読み込みと検証 (2) 定数時間での照合 (3) 認証不要パスの判定。
//
// なぜ認証が要るか:
//   relay は ANTHROPIC_API_KEY を保持したまま LAN に口を開けている。無認証だと
//   同じ Wi-Fi にいる誰でもユーザーのキーで課金でき、/pokemon/* は外部 CDN への踏み台になる。
//   さらに #219 で PC 操作を足すため、ここを閉じないと「LAN の誰でも PC を操作できる」になる。
import { createHash, timingSafeEqual } from "node:crypto";

// 実機（HTTPClient）から載せるヘッダ名。Hono の c.req.header() は大文字小文字を区別しない。
export const TOKEN_HEADER = "x-relay-token";

// トークンの最低長。短すぎる値は総当たりで破られるため起動時に弾く。
// 推奨は 32 文字以上（`openssl rand -hex 16` 等）。
export const MIN_TOKEN_LENGTH = 16;

// テンプレートに書かれている「書き換え前」の値。**長さ検証だけでは防げない穴**を塞ぐために弾く。
// これらは public リポジトリに平文で載っているので、編集し忘れたまま起動すると
// 「誰でも README を読めば正解のトークンが分かる」状態になり、認証が有って無いものになる。
// テンプレート側の値も最低長未満にしてあるが、利用者が長さだけ伸ばして使う事故も併せて防ぐ。
export const PLACEHOLDER_TOKENS = [
  "change_me",
  "changeme",
  "replace-with-a-random-32-hex-string",
  "replace-with-the-same-token-as-relay-env",
  "your-token-here",
] as const;

// 認証を免除するパス。**許可リスト方式**にしてあるのが要点で、
// エンドポイントを増やしても既定で保護される（足し忘れが穴にならない）。
// /health は監視用途で、返すのが {ok:true} だけなので免除してよい。
export const PUBLIC_PATHS = ["/health"] as const;

// 環境変数から共有トークンを読み、妥当性を検証して返す。
// 不正なら理由付きで投げる。呼び出し側（server.ts）はこれを捕まえて**起動を失敗させる**。
//
// 「未設定なら無認証で起動」は採らない。設定を忘れた時に静かに丸腰になるのが最悪だからで、
// 起動しない方が事故として気づける（フェイルクローズ）。
export function loadToken(env: Record<string, string | undefined>): string {
  const raw = env.RELAY_TOKEN;
  if (raw === undefined || raw.trim() === "") {
    throw new Error(
      "RELAY_TOKEN is required. Set it in relay/.env (e.g. `openssl rand -hex 16`).",
    );
  }
  const token = raw.trim();
  // プレースホルダ判定を長さ検証より**先**に置く。テンプレートの値は短くしてあるので
  // 長さでも弾けるが、その場合「16 文字以上にしろ」と言われて長いプレースホルダに
  // 書き換える、という誤った直し方に誘導してしまうため。
  if ((PLACEHOLDER_TOKENS as readonly string[]).includes(token.toLowerCase())) {
    throw new Error(
      "RELAY_TOKEN is still the template placeholder. Generate a real one (e.g. `openssl rand -hex 16`).",
    );
  }
  if (token.length < MIN_TOKEN_LENGTH) {
    throw new Error(
      `RELAY_TOKEN must be at least ${MIN_TOKEN_LENGTH} characters (got ${token.length}).`,
    );
  }
  return token;
}

// 受け取ったトークンが正解と一致するかを**定数時間で**判定する。
//
// なぜ `===` ではないか:
//   文字列比較は最初に違う文字が出た時点で打ち切られるため、応答時間の差から
//   先頭 1 文字ずつ正解を当てられる（タイミング攻撃）。timingSafeEqual は
//   長さが同じバイト列を最後まで比較するのでこの差が出ない。
//
// なぜ SHA-256 を挟むか:
//   timingSafeEqual は長さが違うと例外を投げる。長さを先に比べて早期 return すると
//   「トークンの長さ」だけは漏れるし、例外処理も増える。ハッシュを通せば常に 32 バイトに
//   揃うので、長さに関わらず一切分岐せず比較できる。
export function tokenMatches(
  expected: string,
  actual: string | undefined | null,
): boolean {
  if (typeof actual !== "string" || actual === "") return false;
  const a = createHash("sha256").update(expected, "utf8").digest();
  const b = createHash("sha256").update(actual, "utf8").digest();
  return timingSafeEqual(a, b);
}

// 認証を免除するパスかどうかを判定する。
// **完全一致のみ**で、前方一致は使わない（`/health` を接頭辞にした別パスを素通しさせないため）。
// 末尾スラッシュの揺れだけ吸収する。
export function isPublicPath(pathname: string): boolean {
  const normalized =
    pathname.length > 1 && pathname.endsWith("/")
      ? pathname.slice(0, -1)
      : pathname;
  return (PUBLIC_PATHS as readonly string[]).includes(normalized);
}
