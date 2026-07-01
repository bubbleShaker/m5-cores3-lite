// 中継サーバ /pokemon の「純粋ロジック」。tts.ts / chat.ts と同じく fetch を持たず vitest で完結する。
// 役割は3つ: (1) 図鑑番号(id)の検証 (2) PokeAPI URL 組み立て (3) 生 JSON を実機向けコンパクト JSON へ抽出。

// PokeAPI の全国図鑑番号の想定範囲。第9世代までで 1025 匹（将来増えても弾きすぎない上限）。
export const MIN_POKEMON_ID = 1;
export const MAX_POKEMON_ID = 1025;

// 実機へ返すコンパクトなポケモン情報。~30KB の生 JSON をこの形（<1KB）まで削る。
// フィールドは research/pokeapi-integration.md の /pokemon/info 設計に対応。
export interface PokemonInfo {
  id: number;
  name_ja: string;
  name_en: string;
  category_ja: string; // 分類（例: "たねポケモン"）。PokeAPI では genus と呼ぶ。
  types: string[]; // 英語タイプ名（例: ["grass", "poison"]）。実機側で必要なら和名変換。
  desc_ja: string; // 図鑑説明文（日本語）。
}

// 図鑑番号を検証して数値へ正規化する。不正なら理由付きで投げ、server.ts が 400 にする。
// URL パラメータは文字列で来るため、number/string の両方を受ける。
export function parsePokemonId(raw: unknown): number {
  const n = typeof raw === "number" ? raw : Number(raw);
  if (!Number.isInteger(n) || n < MIN_POKEMON_ID || n > MAX_POKEMON_ID) {
    throw new Error(
      `id must be an integer between ${MIN_POKEMON_ID} and ${MAX_POKEMON_ID}`,
    );
  }
  return n;
}

// 末尾スラッシュの有無を吸収してベース URL を正規化する（tts.ts と同じ方針）。
function normalizeBase(baseUrl: string): string {
  return baseUrl.replace(/\/+$/, "");
}

// GET /pokemon/{id} の URL。タイプやスプライト URL など「種族に依らない個体データ」を持つ。
export function pokemonUrl(baseUrl: string, id: number): string {
  return `${normalizeBase(baseUrl)}/pokemon/${id}`;
}

// GET /pokemon-species/{id} の URL。多言語名・分類(genus)・図鑑説明文を持つ。
export function speciesUrl(baseUrl: string, id: number): string {
  return `${normalizeBase(baseUrl)}/pokemon-species/${id}`;
}

// PokeAPI の多言語配列（names / genera / flavor_text_entries）は
// [{ language: { name: "ja" }, ... }] という共通形。指定言語のエントリを1件拾う。
// 日本語は "ja"（漢字含む）優先、無ければ "ja-Hrkt"（かな）にフォールバック。
type LangEntry<T> = T & { language?: { name?: string } };
function pickByLanguage<T>(
  entries: unknown,
  primary: string,
  fallback?: string,
): LangEntry<T> | undefined {
  if (!Array.isArray(entries)) return undefined;
  const list = entries as LangEntry<T>[];
  const byLang = (lang: string) =>
    list.find((e) => e?.language?.name === lang);
  return byLang(primary) ?? (fallback ? byLang(fallback) : undefined);
}

// 図鑑説明文は改行(\n)や改ページ(\f)が混じる。実機の1行表示向けに、
// 連続する空白類（改行・改ページ・スペース）をまとめて単一スペースへ潰す。
function cleanFlavorText(text: string): string {
  return text.replace(/\s+/g, " ").trim();
}

// pokemon + pokemon-species の生レスポンスから実機向けコンパクト JSON を組み立てる純粋関数。
// 欠損フィールドがあっても落ちないよう、各項目は安全側（空文字/空配列）に倒す。
export function extractPokemonInfo(
  id: number,
  pokemon: Record<string, unknown>,
  species: Record<string, unknown>,
): PokemonInfo {
  // 日本語名: species.names の "ja"（無ければ "ja-Hrkt"）。
  const nameJa = pickByLanguage<{ name?: string }>(
    species.names,
    "ja",
    "ja-Hrkt",
  )?.name;

  // 英語名: species.names の "en"。無ければ pokemon.name（英小文字スラッグ）。
  const nameEn =
    pickByLanguage<{ name?: string }>(species.names, "en")?.name ??
    (typeof pokemon.name === "string" ? pokemon.name : "");

  // 分類(genus): species.genera の "ja"（無ければ "ja-Hrkt"）。
  const genus = pickByLanguage<{ genus?: string }>(
    species.genera,
    "ja",
    "ja-Hrkt",
  )?.genus;

  // 図鑑説明文: species.flavor_text_entries の "ja"（無ければ "ja-Hrkt"）。
  const flavor = pickByLanguage<{ flavor_text?: string }>(
    species.flavor_text_entries,
    "ja",
    "ja-Hrkt",
  )?.flavor_text;

  // タイプ: pokemon.types[].type.name（英語）。slot 順に並べる。
  const types = Array.isArray(pokemon.types)
    ? (pokemon.types as Array<{ type?: { name?: string } }>)
        .map((t) => t?.type?.name)
        .filter((n): n is string => typeof n === "string")
    : [];

  return {
    id,
    name_ja: nameJa ?? "",
    name_en: nameEn,
    category_ja: genus ?? "",
    types,
    desc_ja: flavor ? cleanFlavorText(flavor) : "",
  };
}
