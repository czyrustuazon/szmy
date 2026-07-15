#include "unity.h"
#include "pcm_ring.h"

#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_avail(void)
{
    /* Happy / empty */
    TEST_ASSERT_EQUAL(0, pcm_ring_avail(0, 0));
    TEST_ASSERT_EQUAL(100, pcm_ring_avail(500, 400));
}

static void test_producer_full(void)
{
    /* cap=100, chunk_reserve=20 → full only when used > 80 */
    TEST_ASSERT_FALSE(pcm_ring_producer_full(80, 0, 100, 20)); /* exact edge → not full */
    TEST_ASSERT_TRUE(pcm_ring_producer_full(81, 0, 100, 20));  /* sad: over reserve */
    TEST_ASSERT_FALSE(pcm_ring_producer_full(50, 10, 100, 20)); /* happy: room left */
}

static void test_write_read_linear(void)
{
    uint8_t ring[16];
    uint8_t src[] = { 1, 2, 3, 4, 5 };
    uint8_t dst[5];

    memset(ring, 0, sizeof(ring));
    pcm_ring_write(ring, sizeof(ring), 0, src, sizeof(src));
    pcm_ring_read(ring, sizeof(ring), 0, dst, sizeof(dst));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, 5);
}

static void test_write_read_exact_to_end(void)
{
    /* Happy: w + n == cap uses the non-wrap memcpy arm */
    uint8_t ring[8];
    uint8_t src[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t dst[8];

    memset(ring, 0, sizeof(ring));
    pcm_ring_write(ring, sizeof(ring), 0, src, 8);
    pcm_ring_read(ring, sizeof(ring), 0, dst, 8);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, 8);
}

static void test_write_read_wrap(void)
{
    uint8_t ring[8];
    uint8_t src[] = { 'A', 'B', 'C', 'D', 'E', 'F' };
    uint8_t dst[6];

    memset(ring, 0, sizeof(ring));
    pcm_ring_write(ring, sizeof(ring), 6, src, 6);
    pcm_ring_read(ring, sizeof(ring), 6, dst, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, 6);
}

static void test_prefill_target(void)
{
    /* Happy: non-resume stays under cap/2 (no clamp) */
    TEST_ASSERT_EQUAL(2000, pcm_ring_prefill_target(0, 4096, 1000, 1, 100000));
    /* Non-resume clamps to cap/2 */
    TEST_ASSERT_EQUAL(50000, pcm_ring_prefill_target(0, 4096, 44100, 2, 100000));
    /* Happy resume: 2 * chunk_bytes */
    TEST_ASSERT_EQUAL(8192, pcm_ring_prefill_target(1, 4096, 44100, 2, 100000));
    /* Resume also clamps when cap is small */
    TEST_ASSERT_EQUAL(2048, pcm_ring_prefill_target(1, 4096, 44100, 2, 4096));
}

static void test_samples_from_bytes(void)
{
    /* Happy */
    TEST_ASSERT_EQUAL(4096, pcm_ring_samples_from_bytes(16384, 4));
    TEST_ASSERT_EQUAL(2, pcm_ring_samples_from_bytes(10, 4)); /* leftover bytes dropped */
    /* Sad */
    TEST_ASSERT_EQUAL(0, pcm_ring_samples_from_bytes(3, 4));
    TEST_ASSERT_EQUAL(0, pcm_ring_samples_from_bytes(10, 0));
}

static void test_pop_partial_pads(void)
{
    uint8_t ring[32];
    uint8_t out[16];
    size_t  read_pos = 0;
    size_t  samples;

    memset(ring, 0xAA, sizeof(ring));
    pcm_ring_write(ring, sizeof(ring), 0, (uint8_t[6]){ 1, 2, 3, 4, 5, 6 }, 6);
    samples = pcm_ring_pop_partial(ring, sizeof(ring), 6, &read_pos, out, 16, 4);
    TEST_ASSERT_EQUAL(1, samples);
    TEST_ASSERT_EQUAL_UINT8(1, out[0]);
    TEST_ASSERT_EQUAL_UINT8(2, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0, out[4]);
    TEST_ASSERT_EQUAL(4, read_pos);
}

static void test_pop_partial_exact_chunk(void)
{
    /* Happy: avail fills the whole chunk → zero-pad length is 0 */
    uint8_t ring[32];
    uint8_t out[16];
    size_t  read_pos = 0;
    size_t  samples;
    uint8_t src[16];
    int     i;

    for (i = 0; i < 16; i++)
        src[i] = (uint8_t)(i + 1);
    memset(out, 0xFF, sizeof(out));
    pcm_ring_write(ring, sizeof(ring), 0, src, 16);
    samples = pcm_ring_pop_partial(ring, sizeof(ring), 16, &read_pos, out, 16, 4);
    TEST_ASSERT_EQUAL(4, samples);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, out, 16);
    TEST_ASSERT_EQUAL(16, read_pos);
}

static void test_pop_partial_wrap(void)
{
    uint8_t ring[8];
    uint8_t out[8];
    size_t  read_pos = 6;
    size_t  samples;

    memset(ring, 0, sizeof(ring));
    pcm_ring_write(ring, sizeof(ring), 6, (uint8_t[4]){ 9, 8, 7, 6 }, 4);
    samples = pcm_ring_pop_partial(ring, sizeof(ring), 10, &read_pos, out, 8, 2);
    TEST_ASSERT_EQUAL(2, samples);
    TEST_ASSERT_EQUAL_UINT8(9, out[0]);
    TEST_ASSERT_EQUAL_UINT8(8, out[1]);
    TEST_ASSERT_EQUAL_UINT8(7, out[2]);
    TEST_ASSERT_EQUAL_UINT8(6, out[3]);
    TEST_ASSERT_EQUAL(10, read_pos);
}

static void test_pop_partial_empty_or_zero_frame(void)
{
    /* Sad: nothing to drain / invalid frame size */
    uint8_t ring[8];
    uint8_t out[8];
    size_t  read_pos = 0;

    memset(ring, 0, sizeof(ring));
    memset(out, 0xFF, sizeof(out));
    TEST_ASSERT_EQUAL(0, pcm_ring_pop_partial(ring, sizeof(ring), 0, &read_pos, out, 8, 2));
    TEST_ASSERT_EQUAL(0, read_pos);

    read_pos = 0;
    TEST_ASSERT_EQUAL(0, pcm_ring_pop_partial(ring, sizeof(ring), 4, &read_pos, out, 8, 0));
    TEST_ASSERT_EQUAL(0, read_pos);
}

static void test_roundtrip_multiple_chunks(void)
{
    uint8_t ring[64];
    uint8_t chunk_a[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint8_t chunk_b[8] = { 8, 9, 10, 11, 12, 13, 14, 15 };
    uint8_t out[8];
    size_t  write_pos = 0;
    size_t  read_pos  = 0;

    pcm_ring_write(ring, sizeof(ring), write_pos, chunk_a, 8);
    write_pos += 8;
    pcm_ring_write(ring, sizeof(ring), write_pos, chunk_b, 8);
    write_pos += 8;

    pcm_ring_read(ring, sizeof(ring), read_pos, out, 8);
    read_pos += 8;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(chunk_a, out, 8);

    pcm_ring_read(ring, sizeof(ring), read_pos, out, 8);
    read_pos += 8;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(chunk_b, out, 8);

    TEST_ASSERT_EQUAL(0, pcm_ring_avail(write_pos, read_pos));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_avail);
    RUN_TEST(test_producer_full);
    RUN_TEST(test_write_read_linear);
    RUN_TEST(test_write_read_exact_to_end);
    RUN_TEST(test_write_read_wrap);
    RUN_TEST(test_prefill_target);
    RUN_TEST(test_samples_from_bytes);
    RUN_TEST(test_pop_partial_pads);
    RUN_TEST(test_pop_partial_exact_chunk);
    RUN_TEST(test_pop_partial_wrap);
    RUN_TEST(test_pop_partial_empty_or_zero_frame);
    RUN_TEST(test_roundtrip_multiple_chunks);
    return UNITY_END();
}
