#include "unity.h"
#include "tap_detector.h"

void setUp(void) {}
void tearDown(void) {}

static void settle(tap_detector_t *d, uint64_t impact_ms)
{
    /* First return sample is still a large jerk; the second stable sample
     * rearms after the debounce interval. */
    TEST_ASSERT_FALSE(tap_detector_feed(d, 0, 0, 512, impact_ms + 10));
    TEST_ASSERT_FALSE(tap_detector_feed(d, 0, 0, 512, impact_ms + 90));
}

static void test_null_and_initial_sample(void)
{
    tap_detector_t d;

    tap_detector_reset(NULL);
    TEST_ASSERT_FALSE(tap_detector_feed(NULL, 0, 0, 0, 0));

    tap_detector_reset(&d);
    TEST_ASSERT_FALSE(tap_detector_feed(&d, 0, 0, 512, 0));
    TEST_ASSERT_TRUE(d.ready);
    TEST_ASSERT_TRUE(d.armed);
    TEST_ASSERT_FALSE(tap_detector_feed(&d, 100, 100, 512, 10));
}

static void test_three_separate_taps_trigger_once(void)
{
    tap_detector_t d;

    tap_detector_reset(&d);
    TEST_ASSERT_FALSE(tap_detector_feed(&d, 0, 0, 512, 0));

    TEST_ASSERT_FALSE(tap_detector_feed(&d, 500, 0, 512, 10));
    /* Rebound/high motion while disarmed cannot become another tap. */
    TEST_ASSERT_FALSE(tap_detector_feed(&d, -500, 0, 512, 20));
    TEST_ASSERT_FALSE(tap_detector_feed(&d, 0, 0, 512, 30));
    TEST_ASSERT_FALSE(tap_detector_feed(&d, 0, 0, 512, 100));

    TEST_ASSERT_FALSE(tap_detector_feed(&d, 500, 0, 512, 180));
    settle(&d, 180);

    TEST_ASSERT_TRUE(tap_detector_feed(&d, 500, 0, 512, 350));
    settle(&d, 350);

    /* Detector starts a fresh sequence after reporting. */
    TEST_ASSERT_FALSE(tap_detector_feed(&d, 500, 0, 512, 520));
}

static void test_sequence_timeout_discards_old_taps(void)
{
    tap_detector_t d;

    tap_detector_reset(&d);
    TEST_ASSERT_FALSE(tap_detector_feed(&d, 0, 0, 512, 0));
    TEST_ASSERT_FALSE(tap_detector_feed(&d, 500, 0, 512, 10));
    settle(&d, 10);

    /* More than 1.2 s later: this begins a new sequence, not tap two. */
    TEST_ASSERT_FALSE(tap_detector_feed(&d, 500, 0, 512, 1300));
    TEST_ASSERT_EQUAL(1, d.count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_null_and_initial_sample);
    RUN_TEST(test_three_separate_taps_trigger_once);
    RUN_TEST(test_sequence_timeout_discards_old_taps);
    return UNITY_END();
}
