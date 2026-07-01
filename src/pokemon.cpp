#include "pokemon.h"
#include <ArduinoJson.h>

Pokemon parse_pokemon_info(const std::string& json) {
    Pokemon out;
    out.id = 0;  // パース不能・欠落時の安全既定。

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        // パース不能 → 空の Pokemon を返してデバイスを止めない（フォールバック）。
        return out;
    }

    // doc["key"] | default … キーが無い/null/型違いなら default を返す ArduinoJson のイディオム
    // （net.cpp の parse_relay_reply と同じ書き味に揃える）。id=0 は「取得失敗」を兼ねる番兵。
    out.id          = doc["id"] | 0;
    out.name_ja     = doc["name_ja"] | "";
    out.name_en     = doc["name_en"] | "";
    out.category_ja = doc["category_ja"] | "";
    out.desc_ja     = doc["desc_ja"] | "";

    // types は文字列配列。文字列要素だけを ", " で連結する（非文字列/欠落は黙って飛ばす）。
    JsonArrayConst types = doc["types"].as<JsonArrayConst>();
    for (JsonVariantConst t : types) {
        const char* name = t.as<const char*>();
        if (!name) continue;  // 数値や null などの異物は無視して安全側に。
        if (!out.types.empty()) out.types += ", ";
        out.types += name;
    }

    return out;
}
