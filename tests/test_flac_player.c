#include "unity.h"
#include "audio.h"
#include "audio_ctrl.h"
#include "host_drflac.h"
#include "host_pcm.h"
#include "3ds.h"

void setUp(void)
{
    host_mock_reset();
    audio_ctrl_reset();
    flac_test_set_fake_producer_full(0);
    flac_test_set_decode_spin(0);
}

void tearDown(void) {}

static void test_missing_file(void)
{
    /* Sad: open fails */
    TEST_ASSERT_EQUAL(-1, audio_play_flac("fixtures/no_such.flac"));
    TEST_ASSERT_EQUAL(-1, audio_play_flac(NULL));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_invalid_flac_data(void)
{
    /* Sad: file opens but is not a FLAC stream */
    TEST_ASSERT_EQUAL(-1, audio_play_flac("fixtures/garbage.mp3"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_bad_channels(void)
{
    /* Sad: channel count outside 1..2 */
    TEST_ASSERT_EQUAL(-2, audio_play_flac("bad_channels.flac"));
    TEST_ASSERT_EQUAL(-2, audio_play_flac("zero_channels.flac"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_bad_sample_rate(void)
{
    /* Sad: sample rate outside 300..48000 */
    TEST_ASSERT_EQUAL(-2, audio_play_flac("bad_rate.flac"));
    TEST_ASSERT_EQUAL(-2, audio_play_flac("high_rate.flac"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_accepts_boundary_sample_rates(void)
{
    /* Happy: inclusive low/high sample-rate edges */
    TEST_ASSERT_EQUAL(0, audio_play_flac("rate_low_ok.flac"));
    TEST_ASSERT_EQUAL_FLOAT(300.0f, host_pcm_sample_rate());
    TEST_ASSERT_EQUAL(100, host_pcm_samples_fed());

    host_mock_reset();
    audio_ctrl_reset();
    TEST_ASSERT_EQUAL(0, audio_play_flac("rate_high_ok.flac"));
    TEST_ASSERT_EQUAL_FLOAT(48000.0f, host_pcm_sample_rate());
    TEST_ASSERT_EQUAL(100, host_pcm_samples_fed());
}

static void test_plays_host_fixture(void)
{
    /* Happy: mono play with partial final wavebuf */
    TEST_ASSERT_EQUAL(0, audio_play_flac(HOST_FLAC_FIXTURE));
    TEST_ASSERT_EQUAL(5000, host_pcm_samples_fed());
    TEST_ASSERT_EQUAL(2, host_pcm_buffer_submissions());
    TEST_ASSERT_EQUAL(904, host_pcm_last_chunk_samples());
    TEST_ASSERT_EQUAL(1, host_pcm_channels());
    TEST_ASSERT_EQUAL_FLOAT(44100.0f, host_pcm_sample_rate());
}

static void test_plays_stereo(void)
{
    /* Happy: stereo NDSP format path */
    TEST_ASSERT_EQUAL(0, audio_play_flac("stereo.flac"));
    TEST_ASSERT_EQUAL(2, host_pcm_channels());
    TEST_ASSERT_EQUAL(5000, host_pcm_samples_fed());
}

static void test_thread_create_failure(void)
{
    host_mock_set_thread_fail(1);
    TEST_ASSERT_EQUAL(-4, audio_play_flac(HOST_FLAC_FIXTURE));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_ring_alloc_failure(void)
{
    /* First linearAlloc is the PCM ring → -3 */
    host_mock_set_alloc_fail_after(0);
    TEST_ASSERT_EQUAL(-3, audio_play_flac(HOST_FLAC_FIXTURE));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_wavebuf_alloc_failure(void)
{
    /* After ring (+ decode_buf), a wavebuf linearAlloc fails → -5 (i == 0) */
    host_mock_set_alloc_fail_after(1);
    TEST_ASSERT_EQUAL(-5, audio_play_flac(HOST_FLAC_FIXTURE));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_wavebuf_alloc_failure_frees_prior(void)
{
    /* Allow ring + decode_buf + one wavebuf, then fail → i > 0 so cleanup
     * loop frees already-allocated bufs[j]. */
    host_mock_set_alloc_fail_after(3);
    TEST_ASSERT_EQUAL(-5, audio_play_flac(HOST_FLAC_FIXTURE));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_wavebuf_fail_stops_spinning_decode(void)
{
    /* Decode sits in while(); wavebuf fail sets ring.stop → covers !r->stop false. */
    flac_test_set_decode_spin(1);
    /* ring + decode_buf succeed; first wavebuf fails after decode is in-loop. */
    host_mock_set_alloc_fail_after(2);
    TEST_ASSERT_EQUAL(-5, audio_play_flac(HOST_FLAC_FIXTURE));
    flac_test_set_decode_spin(0);
}

static void test_decode_spin_wait_exits_on_decode_done(void)
{
    /* Wait loop: decode_buf OOM → decode_done without g_decode_in_loop. */
    flac_test_set_decode_spin(1);
    host_mock_set_alloc_fail_after(1);
    TEST_ASSERT_EQUAL(-5, audio_play_flac(HOST_FLAC_FIXTURE));
    flac_test_set_decode_spin(0);
}

static void test_decode_exits_via_should_stop(void)
{
    /* audio_stop() while still spinning → should_exit with r->stop still false. */
    flac_test_set_decode_spin(1);
    flac_test_set_decode_spin_stop_after(3);
    TEST_ASSERT_EQUAL(0, audio_play_flac(HOST_FLAC_FIXTURE));
    flac_test_set_decode_spin(0);
}

static void test_resume_playback(void)
{
    audio_note_paused_at(1000);
    TEST_ASSERT_EQUAL(0, audio_play_flac(HOST_FLAC_FIXTURE));
    TEST_ASSERT_EQUAL(4000, host_pcm_samples_fed());
    TEST_ASSERT_EQUAL(1, host_pcm_channels());
}

static void test_pause_notes_resume_point(void)
{
    /* Pause requested before play → early exit via should_exit; notes resume. */
    audio_ctrl_test_set_playing(1);
    audio_pause();
    TEST_ASSERT_EQUAL(0, audio_play_flac(HOST_FLAC_FIXTURE));
    /* samples_fed is 0 on immediate pause-exit; resume point still recorded */
    TEST_ASSERT_EQUAL_INT64(0, audio_take_resume_sample());
}

static void test_decode_yields_when_ring_full(void)
{
    /* Exercise producer_full sleep/continue without needing a huge ring. */
    flac_test_set_fake_producer_full(2);
    TEST_ASSERT_EQUAL(0, audio_play_flac(HOST_FLAC_FIXTURE));
    TEST_ASSERT_EQUAL(5000, host_pcm_samples_fed());
}

static void test_playback_waits_for_data(void)
{
    /* Slow decode lets the consumer outrun the producer → PLAYBACK_YIELD_US wait. */
    TEST_ASSERT_EQUAL(0, audio_play_flac("slow_mono.flac"));
    TEST_ASSERT_EQUAL(60000, host_pcm_samples_fed());
}

static void test_playback_waits_on_busy_wavebuf(void)
{
    /* decode_done && a!=0 on held buf0 (true,false branch of line 216). */
    host_mock_set_wavebuf_hold(1);
    TEST_ASSERT_EQUAL(0, audio_play_flac("over4.flac"));
    TEST_ASSERT_EQUAL(20480, host_pcm_samples_fed());
}

static void test_playback_busy_while_decoding(void)
{
    /* Hold first wavebuf PLAYING; wrap back to it while decode is still
     * throttled (slow_mono) so decode_done==false short-circuits line 216. */
    host_mock_set_wavebuf_hold(1);
    TEST_ASSERT_EQUAL(0, audio_play_flac("slow_mono.flac"));
    TEST_ASSERT_EQUAL(60000, host_pcm_samples_fed());
}

static void test_playback_ends_when_busy_and_ring_empty(void)
{
    /* Four full chunks: buf0 held PLAYING, bufs 1–3 drain ring; next wraps to 0
     * with decode_done && empty ring → stream_done via busy-buffer path. */
    host_mock_set_wavebuf_hold(1);
    host_mock_set_wavebuf_persist(1);
    TEST_ASSERT_EQUAL(0, audio_play_flac("exact4.flac"));
    TEST_ASSERT_EQUAL(16384, host_pcm_samples_fed());
}

static void test_playback_ends_exact_empty_on_free_buf(void)
{
    /* Happy: full chunks only → decode_done with avail==0 (no partial pop). */
    TEST_ASSERT_EQUAL(0, audio_play_flac("exact4.flac"));
    TEST_ASSERT_EQUAL(16384, host_pcm_samples_fed());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_missing_file);
    RUN_TEST(test_invalid_flac_data);
    RUN_TEST(test_bad_channels);
    RUN_TEST(test_bad_sample_rate);
    RUN_TEST(test_accepts_boundary_sample_rates);
    RUN_TEST(test_plays_host_fixture);
    RUN_TEST(test_plays_stereo);
    RUN_TEST(test_thread_create_failure);
    RUN_TEST(test_ring_alloc_failure);
    RUN_TEST(test_wavebuf_alloc_failure);
    RUN_TEST(test_wavebuf_alloc_failure_frees_prior);
    RUN_TEST(test_wavebuf_fail_stops_spinning_decode);
    RUN_TEST(test_decode_spin_wait_exits_on_decode_done);
    RUN_TEST(test_decode_exits_via_should_stop);
    RUN_TEST(test_resume_playback);
    RUN_TEST(test_pause_notes_resume_point);
    RUN_TEST(test_decode_yields_when_ring_full);
    RUN_TEST(test_playback_waits_for_data);
    RUN_TEST(test_playback_waits_on_busy_wavebuf);
    RUN_TEST(test_playback_busy_while_decoding);
    RUN_TEST(test_playback_ends_when_busy_and_ring_empty);
    RUN_TEST(test_playback_ends_exact_empty_on_free_buf);
    return UNITY_END();
}
