#include "unity.h"
#include "audio_viz.h"

#include <math.h>
#include <string.h>

#define SR 44100

void setUp(void)
{
    audio_viz_reset();
    audio_viz_set_scope_mode(0); /* mode persists across reset */
}

void tearDown(void) {}

/* Band center used by the module: fmin=80, fmax=min(0.45*sr, 12000). */
static float band_freq(int band, int sr)
{
    float fmax = (float)sr * 0.45f;
    float t    = (float)band / (float)(AUDIO_VIZ_BARS - 1);

    if (fmax > 12000.f)
        fmax = 12000.f;
    if (fmax < 120.f)
        fmax = 120.f;
    return 80.f * powf(fmax / 80.f, t);
}

static void fill_sine(int16_t *buf, size_t frames, unsigned ch, float freq,
                      int sr, float amp)
{
    size_t i;
    unsigned c;
    for (i = 0; i < frames; i++) {
        float v = amp * sinf(2.f * 3.14159265f * freq * (float)i / (float)sr);
        for (c = 0; c < ch; c++)
            buf[i * ch + c] = (int16_t)v;
    }
}

static int bars_all_zero(void)
{
    uint8_t bars[AUDIO_VIZ_BARS];
    int i;
    audio_viz_read(bars);
    for (i = 0; i < AUDIO_VIZ_BARS; i++)
        if (bars[i] != 0)
            return 0;
    return 1;
}

static int scope_window_all_zero(uint32_t end_total)
{
    int8_t scope[AUDIO_VIZ_SCOPE];
    int i;
    audio_viz_scope_window(end_total, scope);
    for (i = 0; i < AUDIO_VIZ_SCOPE; i++)
        if (scope[i] != 0)
            return 0;
    return 1;
}

/* Feeds are published AUDIO_VIZ_DELAY chunks late (audibility sync), so
 * repeat a chunk enough times for it to reach the visible levels. */
static void feed_until_visible(const int16_t *pcm, size_t frames, unsigned ch)
{
    int i;
    for (i = 0; i < AUDIO_VIZ_DELAY + 1; i++)
        audio_viz_feed(pcm, frames, ch, SR);
}

static void test_reset_zeroes_bars(void)
{
    TEST_ASSERT_TRUE(bars_all_zero());
}

static void test_invalid_feeds_ignored(void)
{
    int16_t pcm[64] = {0};

    audio_viz_feed(NULL, 32, 1, SR);
    audio_viz_feed(pcm, 0, 1, SR);
    audio_viz_feed(pcm, 32, 0, SR);
    audio_viz_feed(pcm, 32, 3, SR);
    audio_viz_feed(pcm, 32, 1, 0);
    audio_viz_feed(pcm, 32, 1, -44100);
    TEST_ASSERT_TRUE(bars_all_zero());
}

static void test_mono_sine_lights_matching_band(void)
{
    int16_t pcm[512];
    uint8_t bars[AUDIO_VIZ_BARS];
    float   f6 = band_freq(6, SR); /* ~1.2 kHz: many cycles per window */

    fill_sine(pcm, 512, 1, f6, SR, 30000.f);
    feed_until_visible(pcm, 512, 1);
    audio_viz_read(bars);

    TEST_ASSERT_GREATER_THAN(100, bars[6]);
    TEST_ASSERT_GREATER_THAN(bars[0], bars[6]);
    TEST_ASSERT_GREATER_THAN(bars[11], bars[6]);
}

static void test_publish_delayed_until_chunk_audible(void)
{
    int16_t pcm[512];
    int     i;

    fill_sine(pcm, 512, 1, band_freq(6, SR), SR, 30000.f);
    for (i = 0; i < AUDIO_VIZ_DELAY; i++) {
        audio_viz_feed(pcm, 512, 1, SR);
        TEST_ASSERT_TRUE(bars_all_zero());
    }
    audio_viz_feed(pcm, 512, 1, SR); /* oldest chunk now audible */
    TEST_ASSERT_FALSE(bars_all_zero());
}

static void test_only_active_display_is_computed(void)
{
    int16_t pcm[512];

    fill_sine(pcm, 512, 1, band_freq(6, SR), SR, 30000.f);

    /* Bars mode: the scope stream must not advance. */
    feed_until_visible(pcm, 512, 1);
    TEST_ASSERT_FALSE(bars_all_zero());
    TEST_ASSERT_EQUAL_UINT32(0u, audio_viz_scope_total());

    /* Waveform mode: points flow (mono and stereo), bars fall to zero. */
    audio_viz_set_scope_mode(1);
    feed_until_visible(pcm, 512, 1);
    TEST_ASSERT_GREATER_THAN(0, (int)audio_viz_scope_total());
    {
        static int16_t stereo[512 * 2];
        fill_sine(stereo, 512, 2, band_freq(6, SR), SR, 30000.f);
        feed_until_visible(stereo, 512, 2);
    }
    {
        int i;
        for (i = 0; i < 64; i++)
            audio_viz_feed(pcm, 512, 1, SR);
        TEST_ASSERT_TRUE(bars_all_zero());
    }
}

static void test_stereo_downmix_and_window_cap(void)
{
    static int16_t pcm[2048 * 2];
    uint8_t bars[AUDIO_VIZ_BARS];
    float   f6 = band_freq(6, SR);

    /* frames > analysis window → module analyzes the first 512 frames */
    fill_sine(pcm, 2048, 2, f6, SR, 30000.f);
    feed_until_visible(pcm, 2048, 2);
    audio_viz_read(bars);
    TEST_ASSERT_GREATER_THAN(100, bars[6]);
}

static void test_short_chunk_uses_all_frames(void)
{
    int16_t pcm[256];
    uint8_t bars[AUDIO_VIZ_BARS];
    float   f8 = band_freq(8, SR);

    fill_sine(pcm, 256, 1, f8, SR, 30000.f);
    feed_until_visible(pcm, 256, 1);
    audio_viz_read(bars);
    TEST_ASSERT_GREATER_THAN(50, bars[8]);
}

static void test_silence_makes_bars_fall(void)
{
    int16_t loud[512];
    int16_t quiet[512] = {0};
    uint8_t before[AUDIO_VIZ_BARS], after[AUDIO_VIZ_BARS];
    float   f6 = band_freq(6, SR);
    int     i;

    fill_sine(loud, 512, 1, f6, SR, 30000.f);
    feed_until_visible(loud, 512, 1);
    audio_viz_read(before);
    TEST_ASSERT_GREATER_THAN(100, before[6]);

    feed_until_visible(quiet, 512, 1); /* quiet chunk reaches the display */
    audio_viz_read(after);
    TEST_ASSERT_LESS_THAN(before[6], after[6]);

    for (i = 0; i < 64; i++)
        audio_viz_feed(quiet, 512, 1, SR);
    TEST_ASSERT_TRUE(bars_all_zero());
}

static void test_square_wave_clamps_at_full(void)
{
    /* Square fundamental is 4/pi x amplitude, i.e. above 0 dBFS for a
     * full-scale square -> raw level exceeds 255 and must clamp. */
    int16_t pcm[512];
    uint8_t bars[AUDIO_VIZ_BARS];
    float   f6 = band_freq(6, SR);
    int     i;

    for (i = 0; i < 512; i++) {
        float v = sinf(2.f * 3.14159265f * f6 * (float)i / (float)SR);
        pcm[i] = (v >= 0.f) ? 32767 : -32767;
    }
    feed_until_visible(pcm, 512, 1);
    audio_viz_read(bars);
    TEST_ASSERT_EQUAL_UINT8(255, bars[6]);
}

static void test_scope_stream_quiet_signal(void)
{
    /* 5000 amplitude x3 gain ≈ ±58: nonzero without hitting the clamps.
     * One 512-frame chunk publishes 4 stream points; the rest of the
     * window (older history that never existed) must read as zero. */
    int16_t  pcm[512];
    int8_t   scope[AUDIO_VIZ_SCOPE];
    uint32_t total;
    int      i, pts;

    audio_viz_set_scope_mode(1);
    TEST_ASSERT_EQUAL_UINT32(0u, audio_viz_scope_total());
    TEST_ASSERT_TRUE(scope_window_all_zero(0));

    fill_sine(pcm, 512, 1, band_freq(4, SR), SR, 5000.f);
    feed_until_visible(pcm, 512, 1);

    total = audio_viz_scope_total();
    pts   = 512 / AUDIO_VIZ_SCOPE_DECIM;
    TEST_ASSERT_EQUAL_UINT32((uint32_t)pts, total);

    audio_viz_scope_window(total, scope);
    for (i = 0; i < AUDIO_VIZ_SCOPE - pts; i++)
        TEST_ASSERT_EQUAL_INT8(0, scope[i]);
    /* Points near zero crossings are legitimately small; the wave peaks
     * must register without hitting the clamps. */
    {
        int amax = 0;
        for (i = AUDIO_VIZ_SCOPE - pts; i < AUDIO_VIZ_SCOPE; i++) {
            int a = (scope[i] >= 0) ? scope[i] : -scope[i];
            if (a > amax)
                amax = a;
        }
        TEST_ASSERT_GREATER_THAN(20, amax);
        TEST_ASSERT_LESS_THAN(127, amax);
    }
}

static void test_scope_end_total_clamped(void)
{
    int16_t pcm[512];
    int8_t  at_total[AUDIO_VIZ_SCOPE], beyond[AUDIO_VIZ_SCOPE];

    audio_viz_set_scope_mode(1);
    fill_sine(pcm, 512, 1, band_freq(4, SR), SR, 5000.f);
    feed_until_visible(pcm, 512, 1);

    audio_viz_scope_window(audio_viz_scope_total(), at_total);
    audio_viz_scope_window(0xFFFFFFFFu, beyond); /* clamps to total */
    TEST_ASSERT_EQUAL_INT8_ARRAY(at_total, beyond, AUDIO_VIZ_SCOPE);
}

static void test_scope_clamps_and_reset(void)
{
    /* Full-scale 100 Hz sine x3 gain exceeds ±127 and must clamp. The 512
     * frames span just over a full cycle, so both peak polarities appear
     * among the published points (crossings stay small, peaks clamp). */
    int16_t  pcm[512];
    int8_t   scope[AUDIO_VIZ_SCOPE];
    uint32_t total;
    int      i, pts, hi = 0, lo = 0;

    audio_viz_set_scope_mode(1);
    fill_sine(pcm, 512, 1, 100.f, SR, 32000.f);
    feed_until_visible(pcm, 512, 1);

    total = audio_viz_scope_total();
    pts   = 512 / AUDIO_VIZ_SCOPE_DECIM;
    audio_viz_scope_window(total, scope);
    for (i = AUDIO_VIZ_SCOPE - pts; i < AUDIO_VIZ_SCOPE; i++) {
        if (scope[i] == 127)
            hi = 1;
        if (scope[i] == -127)
            lo = 1;
    }
    TEST_ASSERT_TRUE(hi);
    TEST_ASSERT_TRUE(lo);

    audio_viz_reset();
    TEST_ASSERT_EQUAL_UINT32(0u, audio_viz_scope_total());
    TEST_ASSERT_TRUE(scope_window_all_zero(0));
}

static void test_scope_decimation_carry_across_chunks(void)
{
    /* 100-frame chunks don't divide evenly into 128-sample points; the
     * carry must keep the published point count exact: after 8 feeds the
     * first 5 chunks (500 samples) are audible → floor(500/128) = 3. */
    int16_t pcm[100] = {0};
    int     i;

    audio_viz_set_scope_mode(1);
    for (i = 0; i < 8; i++)
        audio_viz_feed(pcm, 100, 1, SR);
    TEST_ASSERT_EQUAL_UINT32((8u - AUDIO_VIZ_DELAY) * 100u
                                 / AUDIO_VIZ_SCOPE_DECIM,
                             audio_viz_scope_total());
}

static void test_scope_giant_chunk_point_cap(void)
{
    /* A chunk bigger than the per-chunk point budget (288 points) stops
     * emitting once full instead of overflowing. */
    static int16_t pcm[8192];
    int i;

    audio_viz_set_scope_mode(1);
    memset(pcm, 0, sizeof(pcm));
    for (i = 0; i < AUDIO_VIZ_DELAY + 1; i++)
        audio_viz_feed(pcm, 8192, 1, SR);
    TEST_ASSERT_EQUAL_UINT32(288u, audio_viz_scope_total());
}

static void test_scope_ring_overwrite_reads_zero(void)
{
    /* Push far more points than the ring holds; windows ending in the
     * overwritten past must read as zero while the newest data survives. */
    int16_t pcm[512];
    int     i;

    audio_viz_set_scope_mode(1);
    fill_sine(pcm, 512, 1, band_freq(4, SR), SR, 5000.f);
    for (i = 0; i < 300; i++)
        audio_viz_feed(pcm, 512, 1, SR);

    TEST_ASSERT_GREATER_THAN(1024, (int)audio_viz_scope_total());
    TEST_ASSERT_TRUE(scope_window_all_zero(100));
    TEST_ASSERT_FALSE(scope_window_all_zero(audio_viz_scope_total()));
}

static void test_coeff_cache_and_rebuild(void)
{
    int16_t pcm[512];
    float   f6 = band_freq(6, SR);

    fill_sine(pcm, 512, 1, f6, SR, 20000.f);
    audio_viz_feed(pcm, 512, 1, SR);     /* builds coeffs */
    audio_viz_feed(pcm, 512, 1, SR);     /* cached-coeff path */
    audio_viz_feed(pcm, 512, 1, 48000);  /* rebuild on rate change */
    audio_viz_feed(pcm, 512, 1, 200);    /* fmax floor clamp (0.45*200 < 120) */
    TEST_ASSERT_FALSE(bars_all_zero());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reset_zeroes_bars);
    RUN_TEST(test_invalid_feeds_ignored);
    RUN_TEST(test_mono_sine_lights_matching_band);
    RUN_TEST(test_publish_delayed_until_chunk_audible);
    RUN_TEST(test_only_active_display_is_computed);
    RUN_TEST(test_stereo_downmix_and_window_cap);
    RUN_TEST(test_short_chunk_uses_all_frames);
    RUN_TEST(test_silence_makes_bars_fall);
    RUN_TEST(test_square_wave_clamps_at_full);
    RUN_TEST(test_scope_stream_quiet_signal);
    RUN_TEST(test_scope_end_total_clamped);
    RUN_TEST(test_scope_clamps_and_reset);
    RUN_TEST(test_scope_decimation_carry_across_chunks);
    RUN_TEST(test_scope_giant_chunk_point_cap);
    RUN_TEST(test_scope_ring_overwrite_reads_zero);
    RUN_TEST(test_coeff_cache_and_rebuild);
    return UNITY_END();
}
