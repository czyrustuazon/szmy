#include "unity.h"
#include "bmp_util.h"

#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

/* Build a 1x1 BMP buffer. pixel[] is 3 bytes (24bpp) or 4 (32bpp). */
static void make_bmp(uint8_t *buf, uint32_t buf_cap, uint32_t *out_size,
                     int32_t height, uint16_t bpp, uint32_t comp,
                     const uint8_t *pixel, uint32_t pixel_len)
{
    uint32_t off = 54u;
    uint32_t size;

    TEST_ASSERT_TRUE(buf_cap >= 54u + pixel_len);
    memset(buf, 0, buf_cap);
    buf[0] = 'B';
    buf[1] = 'M';
    put_u32(buf + 10, off);
    put_u32(buf + 14, 40u); /* BITMAPINFOHEADER */
    put_u32(buf + 18, 1u);  /* width */
    put_u32(buf + 22, (uint32_t)height);
    put_u16(buf + 26, 1u); /* planes */
    put_u16(buf + 28, bpp);
    put_u32(buf + 30, comp);
    memcpy(buf + off, pixel, pixel_len);
    size = off + pixel_len;
    put_u32(buf + 2, size);
    *out_size = size;
}

/* Minimal valid 1x1 24-bit BMP (B=0x10 G=0x20 R=0x30). */
static const uint8_t kBmp1x1[] = {
    'B', 'M', 0x3E, 0, 0, 0, 0, 0, 0, 0, 0x36, 0, 0, 0,
    0x28, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0x18, 0,
    0, 0, 0, 0, 0x04, 0, 0, 0, 0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x10, 0x20, 0x30, 0x00
};

static void test_bmp_open_valid(void)
{
    bmp_view_t b;
    TEST_ASSERT_EQUAL(0, bmp_open(&b, kBmp1x1, sizeof(kBmp1x1)));
    TEST_ASSERT_EQUAL(1u, b.w);
    TEST_ASSERT_EQUAL(1u, b.h);
    TEST_ASSERT_EQUAL(24u, b.bpp);
    TEST_ASSERT_EQUAL(0, b.top_down);
}

static void test_bmp_open_rejects_garbage(void)
{
    bmp_view_t b;
    const uint8_t bad[] = "NOTABMP";
    uint8_t tiny[20];
    uint8_t not_b[64];
    uint8_t bad_m[64];

    memset(tiny, 0, sizeof(tiny));
    tiny[0] = 'B';
    tiny[1] = 'M';

    memset(not_b, 0, sizeof(not_b));
    not_b[0] = 'X';
    not_b[1] = 'M';

    memset(bad_m, 0, sizeof(bad_m));
    bad_m[0] = 'B';
    bad_m[1] = 'X';

    TEST_ASSERT_EQUAL(-1, bmp_open(&b, bad, sizeof(bad))); /* size < 54 */
    TEST_ASSERT_EQUAL(-1, bmp_open(NULL, kBmp1x1, sizeof(kBmp1x1)));
    TEST_ASSERT_EQUAL(-1, bmp_open(&b, NULL, 64));
    TEST_ASSERT_EQUAL(-1, bmp_open(&b, tiny, sizeof(tiny))); /* size < 54 */
    TEST_ASSERT_EQUAL(-1, bmp_open(&b, not_b, sizeof(not_b))); /* data[0] != 'B' */
    TEST_ASSERT_EQUAL(-1, bmp_open(&b, bad_m, sizeof(bad_m))); /* data[1] != 'M' */
}

static void test_bmp_open_rejects_truncated_pixels(void)
{
    bmp_view_t b;
    uint8_t buf[64];
    uint32_t size;
    const uint8_t pix[4] = {0, 0, 0, 0};

    make_bmp(buf, sizeof(buf), &size, 1, 24, 0, pix, 4);
    /* Claim a valid header but shrink the buffer so pixels don't fit. */
    TEST_ASSERT_EQUAL(-1, bmp_open(&b, buf, 54u));
}

static void test_bmp_open_rejects_bad_bpp_and_comp(void)
{
    bmp_view_t b;
    uint8_t buf[64];
    uint32_t size;
    const uint8_t pix24[4] = {1, 2, 3, 0};
    const uint8_t pix32[4] = {1, 2, 3, 4};

    make_bmp(buf, sizeof(buf), &size, 1, 8, 0, pix24, 4);
    TEST_ASSERT_EQUAL(-1, bmp_open(&b, buf, size));

    make_bmp(buf, sizeof(buf), &size, 1, 24, 1, pix24, 4); /* RLE8 */
    TEST_ASSERT_EQUAL(-1, bmp_open(&b, buf, size));

    make_bmp(buf, sizeof(buf), &size, 1, 24, 2, pix24, 4); /* RLE4 */
    TEST_ASSERT_EQUAL(-1, bmp_open(&b, buf, size));

    make_bmp(buf, sizeof(buf), &size, 1, 24, 4, pix24, 4); /* JPEG */
    TEST_ASSERT_EQUAL(-1, bmp_open(&b, buf, size));

    make_bmp(buf, sizeof(buf), &size, 1, 24, 5, pix24, 4); /* PNG */
    TEST_ASSERT_EQUAL(-1, bmp_open(&b, buf, size));

    make_bmp(buf, sizeof(buf), &size, 1, 24, 3, pix24, 4); /* bitfields on 24 */
    TEST_ASSERT_EQUAL(-1, bmp_open(&b, buf, size));

    make_bmp(buf, sizeof(buf), &size, 1, 32, 6, pix32, 4); /* unknown 32-bit comp */
    TEST_ASSERT_EQUAL(-1, bmp_open(&b, buf, size));
}

static void test_bmp_open_top_down_and_32bit(void)
{
    bmp_view_t b;
    uint8_t buf[64];
    uint32_t size;
    const uint8_t pix32[4] = {0x11, 0x22, 0x33, 0x44};

    make_bmp(buf, sizeof(buf), &size, -1, 32, 0, pix32, 4);
    TEST_ASSERT_EQUAL(0, bmp_open(&b, buf, size));
    TEST_ASSERT_EQUAL(1, b.top_down);
    TEST_ASSERT_EQUAL(32u, b.bpp);
    TEST_ASSERT_EQUAL(0u, b.comp);

    make_bmp(buf, sizeof(buf), &size, 1, 32, 3, pix32, 4);
    TEST_ASSERT_EQUAL(0, bmp_open(&b, buf, size));
    TEST_ASSERT_EQUAL(3u, b.comp);
}

static void test_bmp_bgra_reads_pixel(void)
{
    bmp_view_t b;
    uint8_t B, G, R, A;
    bmp_open(&b, kBmp1x1, sizeof(kBmp1x1));
    bmp_bgra(&b, 0, 0, &B, &G, &R, &A);
    TEST_ASSERT_EQUAL_UINT8(0x10, B);
    TEST_ASSERT_EQUAL_UINT8(0x20, G);
    TEST_ASSERT_EQUAL_UINT8(0x30, R);
    TEST_ASSERT_EQUAL_UINT8(255, A);
}

/* Happy: bottom-up vs top-down row order maps display y correctly. */
static void test_bmp_bgra_row_order(void)
{
    bmp_view_t b;
    uint8_t buf[72];
    uint32_t size;
    uint8_t B, G, R, A;
    /* File order for bottom-up: bottom row, then top row. */
    const uint8_t bottom_up[8] = {1, 2, 3, 0, 4, 5, 6, 0};
    /* File order for top-down: top row, then bottom row. */
    const uint8_t top_down[8] = {4, 5, 6, 0, 1, 2, 3, 0};

    make_bmp(buf, sizeof(buf), &size, 2, 24, 0, bottom_up, 8);
    TEST_ASSERT_EQUAL(0, bmp_open(&b, buf, size));
    TEST_ASSERT_EQUAL(0, b.top_down);
    bmp_bgra(&b, 0, 0, &B, &G, &R, &A); /* display top */
    TEST_ASSERT_EQUAL_UINT8(4, B);
    TEST_ASSERT_EQUAL_UINT8(5, G);
    TEST_ASSERT_EQUAL_UINT8(6, R);
    bmp_bgra(&b, 0, 1, &B, &G, &R, &A); /* display bottom */
    TEST_ASSERT_EQUAL_UINT8(1, B);
    TEST_ASSERT_EQUAL_UINT8(2, G);
    TEST_ASSERT_EQUAL_UINT8(3, R);

    make_bmp(buf, sizeof(buf), &size, -2, 24, 0, top_down, 8);
    TEST_ASSERT_EQUAL(0, bmp_open(&b, buf, size));
    TEST_ASSERT_EQUAL(1, b.top_down);
    bmp_bgra(&b, 0, 0, &B, &G, &R, &A);
    TEST_ASSERT_EQUAL_UINT8(4, B);
    TEST_ASSERT_EQUAL_UINT8(5, G);
    TEST_ASSERT_EQUAL_UINT8(6, R);
    bmp_bgra(&b, 0, 1, &B, &G, &R, &A);
    TEST_ASSERT_EQUAL_UINT8(1, B);
    TEST_ASSERT_EQUAL_UINT8(2, G);
    TEST_ASSERT_EQUAL_UINT8(3, R);
}

static void test_bmp_bgra_32bit_alpha_paths(void)
{
    bmp_view_t b;
    uint8_t buf[64];
    uint32_t size;
    uint8_t B, G, R, A;
    uint8_t pix[4];

    /* BI_RGB, A nearly 0, bright RGB -> force opaque */
    pix[0] = 40;
    pix[1] = 40;
    pix[2] = 40;
    pix[3] = 0;
    make_bmp(buf, sizeof(buf), &size, -1, 32, 0, pix, 4);
    TEST_ASSERT_EQUAL(0, bmp_open(&b, buf, size));
    bmp_bgra(&b, 0, 0, &B, &G, &R, &A);
    TEST_ASSERT_EQUAL_UINT8(255, A);

    /* BI_RGB, A nearly 0, dark RGB -> transparent */
    pix[0] = 1;
    pix[1] = 1;
    pix[2] = 1;
    pix[3] = 1;
    make_bmp(buf, sizeof(buf), &size, 1, 32, 0, pix, 4);
    bmp_open(&b, buf, size);
    bmp_bgra(&b, 0, 0, &B, &G, &R, &A);
    TEST_ASSERT_EQUAL_UINT8(0, A);

    /* BI_RGB, real alpha kept */
    pix[0] = 1;
    pix[1] = 2;
    pix[2] = 3;
    pix[3] = 200;
    make_bmp(buf, sizeof(buf), &size, 1, 32, 0, pix, 4);
    bmp_open(&b, buf, size);
    bmp_bgra(&b, 0, 0, &B, &G, &R, &A);
    TEST_ASSERT_EQUAL_UINT8(200, A);

    /* BI_BITFIELDS, A nearly 0, bright -> promote */
    pix[0] = 30;
    pix[1] = 30;
    pix[2] = 30;
    pix[3] = 0;
    make_bmp(buf, sizeof(buf), &size, 1, 32, 3, pix, 4);
    bmp_open(&b, buf, size);
    bmp_bgra(&b, 0, 0, &B, &G, &R, &A);
    TEST_ASSERT_EQUAL_UINT8(255, A);

    /* BI_BITFIELDS, A nearly 0, dark -> leave A */
    pix[0] = 2;
    pix[1] = 2;
    pix[2] = 2;
    pix[3] = 1;
    make_bmp(buf, sizeof(buf), &size, 1, 32, 3, pix, 4);
    bmp_open(&b, buf, size);
    bmp_bgra(&b, 0, 0, &B, &G, &R, &A);
    TEST_ASSERT_EQUAL_UINT8(1, A);

    /* BI_BITFIELDS, real alpha kept (*A >= 2 skips heal branch) */
    pix[0] = 10;
    pix[1] = 20;
    pix[2] = 30;
    pix[3] = 128;
    make_bmp(buf, sizeof(buf), &size, 1, 32, 3, pix, 4);
    bmp_open(&b, buf, size);
    bmp_bgra(&b, 0, 0, &B, &G, &R, &A);
    TEST_ASSERT_EQUAL_UINT8(128, A);
}

static void test_alpha_key_bands(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, bmp_alpha_key_24(0, 0, 0));
    TEST_ASSERT_EQUAL_UINT8(0, bmp_alpha_key_24(2, 2, 3)); /* t=7 → transparent */
    TEST_ASSERT_EQUAL_UINT8(255, bmp_alpha_key_24(200, 100, 50));

    /* Soft band: m < 18 && t < 30 (and t >= 8) */
    TEST_ASSERT_EQUAL_UINT8(15u * 8u, bmp_alpha_key_24(5, 5, 5));

    /* m < 18 but t >= 30 → skip soft band, take mid band */
    TEST_ASSERT_EQUAL_UINT8(80u + 10u * 3u, bmp_alpha_key_24(10, 10, 10));

    /* m >= 18 with t still < 30 → skip soft, take mid */
    TEST_ASSERT_EQUAL_UINT8(80u + 18u * 3u, bmp_alpha_key_24(18, 1, 1));

    /* Mid band: m < 40 but not soft */
    TEST_ASSERT_EQUAL_UINT8(80u + 20u * 3u, bmp_alpha_key_24(20, 20, 20));

    /* Max-channel variants for m selection */
    TEST_ASSERT_EQUAL_UINT8(255, bmp_alpha_key_24(50, 10, 10)); /* b wins */
    TEST_ASSERT_EQUAL_UINT8(255, bmp_alpha_key_24(10, 50, 10)); /* g wins */
    TEST_ASSERT_EQUAL_UINT8(255, bmp_alpha_key_24(10, 10, 50)); /* r wins */
}

static void test_mix565_opaque_replaces(void)
{
    uint16_t out = bmp_mix565(0x0000, 255, 0, 0, 255);
    TEST_ASSERT_NOT_EQUAL(0x0000, out);
}

static void test_mix565_partial_and_zero(void)
{
    /* Sad: fully transparent → destination unchanged */
    TEST_ASSERT_EQUAL_UINT16(0xABCD, bmp_mix565(0xABCD, 255, 0, 0, 0));
    /* Happy: partial blend over black */
    TEST_ASSERT_NOT_EQUAL(0x0000, bmp_mix565(0x0000, 128, 128, 128, 128));
    /* Happy: partial blend over white darkens without going fully black */
    {
        uint16_t white = 0xFFFF;
        uint16_t out = bmp_mix565(white, 0, 0, 0, 128);
        TEST_ASSERT_NOT_EQUAL(white, out);
        TEST_ASSERT_NOT_EQUAL(0x0000, out);
    }
}

static void test_point_in_hitbox(void)
{
    /* Happy: inside padded hitbox */
    TEST_ASSERT_TRUE(bmp_point_in(10, 10, 8, 8, 8, 8, BMP_HIT_PAD_DEFAULT));
    /* Sad: far outside / invalid dimensions */
    TEST_ASSERT_FALSE(bmp_point_in(0, 0, 8, 8, 8, 8, BMP_HIT_PAD_DEFAULT));
    TEST_ASSERT_FALSE(bmp_point_in(10, 10, 8, 8, 0, 8, BMP_HIT_PAD_DEFAULT)); /* w <= 0 */
    TEST_ASSERT_FALSE(bmp_point_in(10, 10, 8, 8, 8, 0, BMP_HIT_PAD_DEFAULT)); /* h <= 0 */

    /* Sad: miss left / right / bottom / top edges */
    TEST_ASSERT_FALSE(bmp_point_in(0, 10, 8, 8, 8, 8, 0));
    TEST_ASSERT_FALSE(bmp_point_in(30, 10, 8, 8, 8, 8, 0));
    TEST_ASSERT_FALSE(bmp_point_in(10, 30, 8, 8, 8, 8, 0));
    TEST_ASSERT_FALSE(bmp_point_in(10, 0, 8, 8, 8, 8, 0)); /* py + pad < y0 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bmp_open_valid);
    RUN_TEST(test_bmp_open_rejects_garbage);
    RUN_TEST(test_bmp_open_rejects_truncated_pixels);
    RUN_TEST(test_bmp_open_rejects_bad_bpp_and_comp);
    RUN_TEST(test_bmp_open_top_down_and_32bit);
    RUN_TEST(test_bmp_bgra_reads_pixel);
    RUN_TEST(test_bmp_bgra_row_order);
    RUN_TEST(test_bmp_bgra_32bit_alpha_paths);
    RUN_TEST(test_alpha_key_bands);
    RUN_TEST(test_mix565_opaque_replaces);
    RUN_TEST(test_mix565_partial_and_zero);
    RUN_TEST(test_point_in_hitbox);
    return UNITY_END();
}
