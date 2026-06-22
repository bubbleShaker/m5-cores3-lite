#pragma once
#include <string>

// Wi-Fi の接続状態（ハード非依存・テストからも参照する）。
enum class WifiState { Connecting, Connected, Failed };

// 接続状態を画面に出す文言に変換する純粋関数。
// 実際の接続処理(WiFi.begin)は main.cpp 側に置き、ここは表示文言だけに責務を絞る。
std::string wifi_status_text(WifiState state);
