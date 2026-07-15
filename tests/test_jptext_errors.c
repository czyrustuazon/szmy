#include "unity.h"
#include "jptext_errors.h"

void setUp(void) {}
void tearDown(void) {}

/* Happy: every defined code maps to its string. */
static void test_known_codes(void)
{
    TEST_ASSERT_EQUAL_STRING("cfg:u", jptext_error_str(1));
    TEST_ASSERT_EQUAL_STRING("C3D", jptext_error_str(2));
    TEST_ASSERT_EQUAL_STRING("C2D", jptext_error_str(3));
    TEST_ASSERT_EQUAL_STRING("screen target", jptext_error_str(4));
    TEST_ASSERT_EQUAL_STRING("system font", jptext_error_str(5));
    TEST_ASSERT_EQUAL_STRING("text buffer", jptext_error_str(6));
    TEST_ASSERT_EQUAL_STRING("background", jptext_error_str(7));
}

/* Sad: unknown / out-of-range codes fall through to "?". */
static void test_unknown_code(void)
{
    TEST_ASSERT_EQUAL_STRING("?", jptext_error_str(0));
    TEST_ASSERT_EQUAL_STRING("?", jptext_error_str(8));
    TEST_ASSERT_EQUAL_STRING("?", jptext_error_str(-1));
    TEST_ASSERT_EQUAL_STRING("?", jptext_error_str(99));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_known_codes);
    RUN_TEST(test_unknown_code);
    return UNITY_END();
}
