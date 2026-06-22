#include <unity.h>
#include <string>
#include "net.h"

void setUp(void) {}
void tearDown(void) {}

// 接続状態ごとに、画面に出す文言が決まる
void test_status_text_connecting() {
    TEST_ASSERT_EQUAL_STRING("Wi-Fi: connecting...",
                             wifi_status_text(WifiState::Connecting).c_str());
}

void test_status_text_connected() {
    TEST_ASSERT_EQUAL_STRING("Wi-Fi: connected",
                             wifi_status_text(WifiState::Connected).c_str());
}

void test_status_text_failed() {
    TEST_ASSERT_EQUAL_STRING("Wi-Fi: failed",
                             wifi_status_text(WifiState::Failed).c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_status_text_connecting);
    RUN_TEST(test_status_text_connected);
    RUN_TEST(test_status_text_failed);
    return UNITY_END();
}
