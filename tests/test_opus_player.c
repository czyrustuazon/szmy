#include "unity.h"
#include "audio.h"
#include "audio_ctrl.h"
#include "host_opusfile.h"
#include "host_pcm.h"
#include "3ds.h"

void setUp(void)
{
    host_mock_reset();
    audio_ctrl_reset();
    opus_test_set_fake_producer_full(0);
    opus_test_set_decode_spin(0);
}

void tearDown(void) {}

static void test_missing_file(void)
{
    TEST_ASSERT_EQUAL(-1, audio_play_opus("fixtures/no_such.opus"));
    TEST_ASSERT_EQUAL(-1, audio_play_opus(NULL));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_invalid_opus_data(void)
{
    TEST_ASSERT_EQUAL(-1, audio_play_opus("fixtures/garbage.mp3"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_bad_channels(void)
{
    TEST_ASSERT_EQUAL(-4, audio_play_opus("bad_channels.opus"));
    TEST_ASSERT_EQUAL(-4, audio_play_opus("zero_channels.opus"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_plays_host_fixture(void)
{
    TEST_ASSERT_EQUAL(0, audio_play_opus(HOST_OPUS_FIXTURE));
    TEST_ASSERT_EQUAL(5000, host_pcm_samples_fed());
    TEST_ASSERT_EQUAL(2, host_pcm_buffer_submissions());
    TEST_ASSERT_EQUAL(904, host_pcm_last_chunk_samples());
    TEST_ASSERT_EQUAL(1, host_pcm_channels());
    TEST_ASSERT_EQUAL_FLOAT(48000.0f, host_pcm_sample_rate());
}

static void test_plays_stereo(void)
{
    TEST_ASSERT_EQUAL(0, audio_play_opus("stereo.opus"));
    TEST_ASSERT_EQUAL(2, host_pcm_channels());
    TEST_ASSERT_EQUAL(5000, host_pcm_samples_fed());
}

static void test_unknown_total_duration(void)
{
    TEST_ASSERT_EQUAL(0, audio_play_opus("unknown_total.opus"));
    TEST_ASSERT_EQUAL(4096, host_pcm_samples_fed());
}

static void test_hole_then_data(void)
{
    TEST_ASSERT_EQUAL(0, audio_play_opus("hole_then_data.opus"));
    TEST_ASSERT_EQUAL(5000, host_pcm_samples_fed());
}

static void test_bad_packet_ends_decode(void)
{
    /* Non-HOLE op_read error breaks the read loop with got==0 → silent end. */
    TEST_ASSERT_EQUAL(0, audio_play_opus("bad_packet.opus"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_seek_failure_on_resume(void)
{
    audio_note_paused_at(1000);
    TEST_ASSERT_EQUAL(-2, audio_play_opus("seek_fail.opus"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_thread_create_failure(void)
{
    host_mock_set_thread_fail(1);
    TEST_ASSERT_EQUAL(-7, audio_play_opus(HOST_OPUS_FIXTURE));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_ring_alloc_failure(void)
{
    host_mock_set_alloc_fail_after(0);
    TEST_ASSERT_EQUAL(-5, audio_play_opus(HOST_OPUS_FIXTURE));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_wavebuf_alloc_failure(void)
{
    host_mock_set_alloc_fail_after(1);
    TEST_ASSERT_EQUAL(-5, audio_play_opus(HOST_OPUS_FIXTURE));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_wavebuf_alloc_failure_frees_prior(void)
{
    host_mock_set_alloc_fail_after(3);
    TEST_ASSERT_EQUAL(-5, audio_play_opus(HOST_OPUS_FIXTURE));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_wavebuf_fail_stops_spinning_decode(void)
{
    opus_test_set_decode_spin(1);
    host_mock_set_alloc_fail_after(2);
    TEST_ASSERT_EQUAL(-5, audio_play_opus(HOST_OPUS_FIXTURE));
    opus_test_set_decode_spin(0);
}

static void test_decode_spin_wait_exits_on_decode_done(void)
{
    opus_test_set_decode_spin(1);
    host_mock_set_alloc_fail_after(1);
    TEST_ASSERT_EQUAL(-5, audio_play_opus(HOST_OPUS_FIXTURE));
    opus_test_set_decode_spin(0);
}

static void test_decode_exits_via_should_stop(void)
{
    opus_test_set_decode_spin(1);
    opus_test_set_decode_spin_stop_after(3);
    TEST_ASSERT_EQUAL(0, audio_play_opus(HOST_OPUS_FIXTURE));
    opus_test_set_decode_spin(0);
}

static void test_resume_playback(void)
{
    audio_note_paused_at(1000);
    TEST_ASSERT_EQUAL(0, audio_play_opus(HOST_OPUS_FIXTURE));
    TEST_ASSERT_EQUAL(4000, host_pcm_samples_fed());
    TEST_ASSERT_EQUAL(1, host_pcm_channels());
}

static void test_pause_notes_resume_point(void)
{
    audio_ctrl_test_set_playing(1);
    audio_pause();
    TEST_ASSERT_EQUAL(0, audio_play_opus(HOST_OPUS_FIXTURE));
    TEST_ASSERT_EQUAL_INT64(0, audio_take_resume_sample());
}

static void test_decode_yields_when_ring_full(void)
{
    opus_test_set_fake_producer_full(2);
    TEST_ASSERT_EQUAL(0, audio_play_opus(HOST_OPUS_FIXTURE));
    TEST_ASSERT_EQUAL(5000, host_pcm_samples_fed());
}

static void test_playback_waits_for_data(void)
{
    TEST_ASSERT_EQUAL(0, audio_play_opus("slow_mono.opus"));
    TEST_ASSERT_EQUAL(60000, host_pcm_samples_fed());
}

static void test_playback_waits_on_busy_wavebuf(void)
{
    host_mock_set_wavebuf_hold(1);
    TEST_ASSERT_EQUAL(0, audio_play_opus("over4.opus"));
    TEST_ASSERT_EQUAL(20480, host_pcm_samples_fed());
}

static void test_playback_busy_while_decoding(void)
{
    host_mock_set_wavebuf_hold(1);
    TEST_ASSERT_EQUAL(0, audio_play_opus("slow_mono.opus"));
    TEST_ASSERT_EQUAL(60000, host_pcm_samples_fed());
}

static void test_playback_ends_when_busy_and_ring_empty(void)
{
    host_mock_set_wavebuf_hold(1);
    host_mock_set_wavebuf_persist(1);
    TEST_ASSERT_EQUAL(0, audio_play_opus("exact4.opus"));
    TEST_ASSERT_EQUAL(16384, host_pcm_samples_fed());
}

static void test_playback_ends_exact_empty_on_free_buf(void)
{
    TEST_ASSERT_EQUAL(0, audio_play_opus("exact4.opus"));
    TEST_ASSERT_EQUAL(16384, host_pcm_samples_fed());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_missing_file);
    RUN_TEST(test_invalid_opus_data);
    RUN_TEST(test_bad_channels);
    RUN_TEST(test_plays_host_fixture);
    RUN_TEST(test_plays_stereo);
    RUN_TEST(test_unknown_total_duration);
    RUN_TEST(test_hole_then_data);
    RUN_TEST(test_bad_packet_ends_decode);
    RUN_TEST(test_seek_failure_on_resume);
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
