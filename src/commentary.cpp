#include "commentary.h"

namespace {

// Gem の各フィールドは const char*（理論上 nullptr もあり得る）。null と空文字を同じ「無し」に畳む。
std::string safe(const char* s) {
    return s ? std::string(s) : std::string();
}

// 事実の一節を「label + value」の形で acc に足す。value が空なら何もしない（欠落項目を静かに省く）。
// 2件目以降は読点「、」で区切る（先頭には付けない）ので、非空の項目だけが自然に連結される。
void append_clause(std::string& acc, const char* label, const std::string& value) {
    if (value.empty()) return;
    if (!acc.empty()) acc += "、";
    acc += label;
    acc += value;
}

}  // namespace

std::string gem_commentary(const Gem& g) {
    const std::string name = safe(g.name);
    // 名前が無いときだけ既定の呼称へフォールバック（他項目は「省く」で対応する）。
    std::string out = (name.empty() ? std::string("この宝石") : name) + "です。";

    // 事実項目（産地・組成・元素）を、非空のものだけ読点で連結する。
    std::string facts;
    append_clause(facts, "産地は",    safe(g.locality));
    append_clause(facts, "組成は",    safe(g.formula));
    append_clause(facts, "主な元素は", safe(g.elements));
    if (!facts.empty()) out += facts + "です。";

    return out;
}

std::string pokemon_commentary(const Pokemon& p) {
    // Pokemon は JSON 由来で std::string 所有。空文字＝欠落として扱う（parse_pokemon_info の既定値と一致）。
    std::string out = p.name_ja.empty() ? std::string("このポケモン") : p.name_ja;
    if (!p.category_ja.empty()) out += "、" + p.category_ja;  // 分類（例: たねポケモン）
    out += "です。";

    if (!p.types.empty())  out += "タイプは" + p.types + "。";  // 例: "grass, poison"
    if (!p.desc_ja.empty()) out += p.desc_ja;                   // 図鑑説明文（それ自体が完結した文）

    return out;
}
