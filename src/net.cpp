#include "net.h"
#include <ArduinoJson.h>

std::string wifi_status_text(WifiState state) {
    switch (state) {
        case WifiState::Connecting: return "Wi-Fi: connecting...";
        case WifiState::Connected:  return "Wi-Fi: connected";
        case WifiState::Failed:     return "Wi-Fi: failed";
    }
    return "Wi-Fi: failed";  // 到達しないが、全分岐を安全側に閉じる
}

ReplyMessage parse_relay_reply(const std::string& json) {
    ReplyMessage out;
    out.expression = Expression::Neutral;
    out.action = "none";

    // JsonDocument は v7 で必要な分だけ伸びる動的ドキュメント。
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        // パース不能 → 全文を reply 扱いにしてデバイスを止めない（フォールバック）。
        out.reply = json;
        return out;
    }

    // doc["key"] | default … キーが無い/型違いなら default を返す ArduinoJson のイディオム。
    out.reply      = doc["reply"].as<const char*>() ? doc["reply"].as<const char*>() : "";
    out.expression = parse_expression(doc["expression"] | "neutral");
    const std::string action = doc["action"] | "none";
    out.action = (action == "notify") ? "notify" : "none";  // 語彙外は none に丸める
    return out;
}
