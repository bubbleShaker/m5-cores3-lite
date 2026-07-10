#pragma once
#include <string>
#include "face_logic.h"  // Expression / parse_expression を再利用する

// Wi-Fi の接続状態（ハード非依存・テストからも参照する）。
enum class WifiState { Connecting, Connected, Failed };

// 接続状態を画面に出す文言に変換する純粋関数。
// 実際の接続処理(WiFi.begin)は main.cpp 側に置き、ここは表示文言だけに責務を絞る。
std::string wifi_status_text(WifiState state);

// 中継サーバ(/chat)の応答を表す構造体。デバイスが信用してよい「関所通過後」の形。
//   reply      … 画面に出す返答文
//   expression … 表情語彙へ正規化済み（未知は Neutral）
//   action     … "none" / "notify"（未知は "none"）
struct ReplyMessage {
    std::string reply;
    Expression  expression;
    std::string action;
};

// 中継サーバの応答 JSON 文字列を ReplyMessage に変換する純粋関数。
// サーバ側 chat.ts の parseClaudeReply と同じ思想：
//   - パース不能でも落とさず、全文を reply 扱いにフォールバック
//   - expression/action は語彙外なら安全側(Neutral/"none")へ正規化
// ArduinoJson はヘッダオンリーで native でも動くため、実機と同一コードをテストできる。
ReplyMessage parse_relay_reply(const std::string& json);
