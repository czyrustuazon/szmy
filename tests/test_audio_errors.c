#include "unity.h"
#include "audio.h"

void setUp(void) {}
void tearDown(void) {}

/* Happy: success code has no message. */
static void test_error_message_success_is_null(void)
{
    TEST_ASSERT_NULL(audio_error_message(0));
}

/* Happy: every defined error code maps to its string. */
static void test_error_message_known_codes(void)
{
    TEST_ASSERT_EQUAL_STRING("Cannot open file", audio_error_message(-1));
    TEST_ASSERT_EQUAL_STRING("Cannot read audio stream", audio_error_message(-2));
    TEST_ASSERT_EQUAL_STRING("Unsupported format", audio_error_message(-3));
    TEST_ASSERT_EQUAL_STRING("Unsupported channels/sample rate", audio_error_message(-4));
    TEST_ASSERT_EQUAL_STRING("Out of memory", audio_error_message(-5));
    TEST_ASSERT_EQUAL_STRING("Could not start playback", audio_error_message(-7));
}

/* Sad: unknown / undefined codes fall through to the default message.
 * Includes -6 (intentional gap between -5 and -7), nearby -8, and far outliers. */
static void test_error_message_unknown_code(void)
{
    TEST_ASSERT_EQUAL_STRING("Playback failed", audio_error_message(-6));
    TEST_ASSERT_EQUAL_STRING("Playback failed", audio_error_message(-8));
    TEST_ASSERT_EQUAL_STRING("Playback failed", audio_error_message(-99));
    TEST_ASSERT_EQUAL_STRING("Playback failed", audio_error_message(42));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_error_message_success_is_null);
    RUN_TEST(test_error_message_known_codes);
    RUN_TEST(test_error_message_unknown_code);
    return UNITY_END();
}
