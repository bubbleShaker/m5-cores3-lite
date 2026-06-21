#include <unity.h>
#include "greeting.h"

// Unity が各テストの前後に呼ぶフック（今回は空でよい）
void setUp(void) {}
void tearDown(void) {}

// 名前を渡すと "Hello, <name>!" になる
void test_make_greeting_with_name() {
    TEST_ASSERT_EQUAL_STRING("Hello, CoreS3-Lite!",
                             make_greeting("CoreS3-Lite").c_str());
}

// 空文字のときは "World" を既定値に使う
void test_make_greeting_empty_defaults_to_world() {
    TEST_ASSERT_EQUAL_STRING("Hello, World!",
                             make_greeting("").c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_make_greeting_with_name);
    RUN_TEST(test_make_greeting_empty_defaults_to_world);
    return UNITY_END();
}
