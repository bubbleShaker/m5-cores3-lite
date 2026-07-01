#pragma once
#include <string>

// ポケモン図鑑の実機表示データ（テーマ N / epic #27・P4・#80）。
// 宝石図鑑(gem)と違い「事実テーブル」を自前で持てない（著作物なので）ため、
// 実行時に中継サーバ /pokemon/info から JSON を取得し、この構造体へ写して表示する。
//
// 中継 relay/src/pokemon.ts の PokemonInfo と 1:1 対応:
//   { id, name_ja, name_en, category_ja, types[], desc_ja }
// types は relay では英語名の配列。実機ではカンマ区切り1本の文字列へ join して持つ
//   （表示は「grass, poison」の素通し。和名変換は行わない＝純粋層を最小に保つ）。
//
// JSON 由来の文字列は取得のたびに寿命が切れるため、gem の const char* ではなく
// 実体を所有する std::string で持つ（parse_relay_reply / ReplyMessage と同じ思想）。
struct Pokemon {
    int         id;           // 図鑑番号（1..）。パース不能時は 0。
    std::string name_ja;      // 和名（例: フシギダネ）
    std::string name_en;      // 英名（例: bulbasaur）
    std::string category_ja;  // 分類（例: たねポケモン）。PokeAPI の genus。
    std::string types;        // タイプをカンマ区切りに join（例: "grass, poison"）
    std::string desc_ja;      // 図鑑説明文（日本語）
};

// 中継 /pokemon/info の応答 JSON を Pokemon に変換する純粋関数。
// parse_relay_reply と同じ設計:
//   - パース不能でも落とさず、全フィールド安全側の既定値（id=0・空文字）で返す
//   - types 配列は文字列要素だけを ", " で連結する（非文字列/欠落は無視）
// ArduinoJson はヘッダオンリーで native でも動くため、実機と同一コードをテストできる。
Pokemon parse_pokemon_info(const std::string& json);
