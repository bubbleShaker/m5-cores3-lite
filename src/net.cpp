#include "net.h"

std::string wifi_status_text(WifiState state) {
    switch (state) {
        case WifiState::Connecting: return "Wi-Fi: connecting...";
        case WifiState::Connected:  return "Wi-Fi: connected";
        case WifiState::Failed:     return "Wi-Fi: failed";
    }
    return "Wi-Fi: failed";  // 到達しないが、全分岐を安全側に閉じる
}
