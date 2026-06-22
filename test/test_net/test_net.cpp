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

// --- parse_relay_reply（中継サーバ応答のパース＆検証） ---

// 正しい JSON はそのままパースされる
void test_parse_reply_valid() {
    ReplyMessage m = parse_relay_reply(
        R"({"reply":"やあ","expression":"happy","action":"none"})");
    TEST_ASSERT_EQUAL_STRING("やあ", m.reply.c_str());
    TEST_ASSERT_TRUE(m.expression == Expression::Happy);
    TEST_ASSERT_EQUAL_STRING("none", m.action.c_str());
}

// action: notify を拾える
void test_parse_reply_notify() {
    ReplyMessage m = parse_relay_reply(
        R"({"reply":"x","expression":"thinking","action":"notify"})");
    TEST_ASSERT_TRUE(m.expression == Expression::Thinking);
    TEST_ASSERT_EQUAL_STRING("notify", m.action.c_str());
}

// 語彙外 expression は Neutral に倒す
void test_parse_reply_unknown_expression() {
    ReplyMessage m = parse_relay_reply(
        R"({"reply":"x","expression":"angry","action":"none"})");
    TEST_ASSERT_TRUE(m.expression == Expression::Neutral);
}

// 語彙外 action は none に倒す
void test_parse_reply_unknown_action() {
    ReplyMessage m = parse_relay_reply(
        R"({"reply":"x","expression":"sad","action":"alert"})");
    TEST_ASSERT_EQUAL_STRING("none", m.action.c_str());
}

// パース不能なテキストは全文を reply 扱い・安全側の既定値
void test_parse_reply_invalid_json() {
    ReplyMessage m = parse_relay_reply("ただのテキスト");
    TEST_ASSERT_EQUAL_STRING("ただのテキスト", m.reply.c_str());
    TEST_ASSERT_TRUE(m.expression == Expression::Neutral);
    TEST_ASSERT_EQUAL_STRING("none", m.action.c_str());
}

// reply 欠落時は空文字
void test_parse_reply_missing_reply() {
    ReplyMessage m = parse_relay_reply(R"({"expression":"surprised"})");
    TEST_ASSERT_EQUAL_STRING("", m.reply.c_str());
    TEST_ASSERT_TRUE(m.expression == Expression::Surprised);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_status_text_connecting);
    RUN_TEST(test_status_text_connected);
    RUN_TEST(test_status_text_failed);
    RUN_TEST(test_parse_reply_valid);
    RUN_TEST(test_parse_reply_notify);
    RUN_TEST(test_parse_reply_unknown_expression);
    RUN_TEST(test_parse_reply_unknown_action);
    RUN_TEST(test_parse_reply_invalid_json);
    RUN_TEST(test_parse_reply_missing_reply);
    return UNITY_END();
}
