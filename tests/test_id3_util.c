#include "unity.h"
#include "id3_util.h"

#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Sad: header incomplete */
static void test_skip_returns_zero_when_buffer_too_small(void)
{
    const uint8_t tiny[] = { 'I', 'D', '3' };
    const uint8_t almost[9] = { 'I', 'D', '3', 0, 0, 0, 0, 0, 0 };

    TEST_ASSERT_EQUAL_UINT(0, id3_skip_tag(tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_UINT(0, id3_skip_tag(almost, sizeof(almost)));
    TEST_ASSERT_EQUAL_UINT(0, id3_skip_tag(almost, 0));
}

/* Sad: not an ID3v2 header */
static void test_skip_returns_zero_without_id3_magic(void)
{
    const uint8_t tag[] = "TAGextra000";
    const uint8_t lower[10] = {
        'i', 'd', '3', 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    const uint8_t partial[10] = {
        'I', 'D', '2', 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    TEST_ASSERT_EQUAL_UINT(0, id3_skip_tag(tag, sizeof(tag)));
    TEST_ASSERT_EQUAL_UINT(0, id3_skip_tag(lower, sizeof(lower)));
    TEST_ASSERT_EQUAL_UINT(0, id3_skip_tag(partial, sizeof(partial)));
}

/* Happy: empty tag body */
static void test_skip_empty_tag(void)
{
    const uint8_t data[] = {
        'I', 'D', '3', 0x03, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    TEST_ASSERT_EQUAL_UINT(10, id3_skip_tag(data, sizeof(data)));
}

/* Happy: small non-zero size in lowest synchsafe byte */
static void test_skip_nonzero_synchsafe_size(void)
{
    const uint8_t data[] = {
        'I', 'D', '3', 0x04, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x05
    };
    TEST_ASSERT_EQUAL_UINT(15, id3_skip_tag(data, sizeof(data)));
}

/* Happy: high bit cleared in each synchsafe byte */
static void test_skip_ignores_high_bit_in_synchsafe_bytes(void)
{
    const uint8_t data[] = {
        'I', 'D', '3', 0x03, 0x00, 0x00,
        0x80, 0x80, 0x80, 0x00
    };
    TEST_ASSERT_EQUAL_UINT(10, id3_skip_tag(data, sizeof(data)));
}

/* Happy: each synchsafe size byte contributes */
static void test_skip_synchsafe_uses_all_size_bytes(void)
{
    const uint8_t data[] = {
        'I', 'D', '3', 0x03, 0x00, 0x00,
        0x01, 0x02, 0x03, 0x04
    };
    size_t expected = 10u + ((1u << 21) | (2u << 14) | (3u << 7) | 4u);

    TEST_ASSERT_EQUAL_UINT(expected, id3_skip_tag(data, sizeof(data)));
}

/* Happy: larger tag size within a buffer */
static void test_skip_large_tag_within_buffer(void)
{
    uint8_t data[32];
    memset(data, 0, sizeof(data));
    memcpy(data, "ID3", 3);
    data[3] = 0x03;
    data[9] = 0x16;
    TEST_ASSERT_EQUAL_UINT(32, id3_skip_tag(data, sizeof(data)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_skip_returns_zero_when_buffer_too_small);
    RUN_TEST(test_skip_returns_zero_without_id3_magic);
    RUN_TEST(test_skip_empty_tag);
    RUN_TEST(test_skip_nonzero_synchsafe_size);
    RUN_TEST(test_skip_ignores_high_bit_in_synchsafe_bytes);
    RUN_TEST(test_skip_synchsafe_uses_all_size_bytes);
    RUN_TEST(test_skip_large_tag_within_buffer);
    return UNITY_END();
}
