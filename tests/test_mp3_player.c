#include "unity.h"
#include "audio.h"
#include "audio_ctrl.h"
#include "host_pcm.h"
#include "3ds.h"

#include <stdio.h>
#include <string.h>

void mp3_test_set_fseek_fail(int fail);
void mp3_test_set_fread_fail(int fail);
void mp3_test_clear_probe_steps(void);
void mp3_test_add_probe_step(int samples, int frame_bytes, int hz, int channels);
void mp3_test_set_decode_alloc_fail(int fail_decode_buf, int fail_pcm16);
void mp3_test_set_fake_producer_full(int times);
void mp3_test_set_decode_spin(int spin);
void mp3_test_set_decode_spin_stop_after(int iters);
void mp3_test_set_decode_frame_bytes_zero(int fail);
void mp3_test_set_format_override(int enable, unsigned int ch, unsigned int sr);
void mp3_test_set_skip_frame_bytes_zero(int fail);
void mp3_test_set_decode_throttle(int times, int ns);
void mp3_test_set_decode_max_chunks(int n);

void setUp(void)
{
    host_mock_reset();
    audio_ctrl_reset();
    mp3_test_set_fseek_fail(0);
    mp3_test_set_fread_fail(0);
    mp3_test_clear_probe_steps();
    mp3_test_set_decode_alloc_fail(0, 0);
    mp3_test_set_fake_producer_full(0);
    mp3_test_set_decode_spin(0);
    mp3_test_set_decode_frame_bytes_zero(0);
    mp3_test_set_format_override(0, 0, 0);
    mp3_test_set_skip_frame_bytes_zero(0);
    mp3_test_set_decode_throttle(0, 0);
    mp3_test_set_decode_max_chunks(0);
}

void tearDown(void) {}

static void write_empty(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f)
        fclose(f);
}

static void test_missing_file(void)
{
    /* Sad: open / null path failures */
    TEST_ASSERT_EQUAL(-1, audio_play_mp3("fixtures/no_such.mp3"));
    TEST_ASSERT_EQUAL(-1, audio_play_mp3(NULL));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_empty_file(void)
{
    write_empty("fixtures/empty.mp3");
    TEST_ASSERT_EQUAL(-1, audio_play_mp3("fixtures/empty.mp3"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_fseek_failure(void)
{
    mp3_test_set_fseek_fail(1);
    TEST_ASSERT_EQUAL(-1, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_file_buf_alloc_failure(void)
{
    /* First linearAlloc is the file image in load_mp3_file. */
    host_mock_set_alloc_fail_after(0);
    TEST_ASSERT_EQUAL(-1, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_fread_failure(void)
{
    mp3_test_set_fread_fail(1);
    TEST_ASSERT_EQUAL(-1, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_probe_frame_bytes_zero(void)
{
    mp3_test_add_probe_step(100, 0, 44100, 2);
    TEST_ASSERT_EQUAL(-2, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_probe_partial_frame_info(void)
{
    /* Exercise each failed arm of samples>0 && hz>0 && channels>0, then quit. */
    mp3_test_add_probe_step(0, 4, 44100, 2);   /* samples <= 0 */
    mp3_test_add_probe_step(100, 4, 0, 2);     /* hz <= 0 */
    mp3_test_add_probe_step(100, 4, 44100, 0); /* channels <= 0 */
    mp3_test_add_probe_step(0, 0, 0, 0);       /* frame_bytes <= 0 → break */
    TEST_ASSERT_EQUAL(-2, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_probe_step_overflow_ignored(void)
{
    int i;
    for (i = 0; i < 8; i++)
        mp3_test_add_probe_step(0, 4, 44100, 2);
    /* 9th add hits g_probe_nsteps >= MP3_PROBE_MAX_STEPS and is dropped. */
    mp3_test_add_probe_step(100, 4, 44100, 2);
    TEST_ASSERT_EQUAL(-2, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_probe_exhausts_injected_steps(void)
{
    /* All steps fail success predicate; next loop hits g_probe_i >= g_probe_nsteps. */
    mp3_test_add_probe_step(0, 4, 44100, 2);
    mp3_test_add_probe_step(0, 4, 44100, 2);
    TEST_ASSERT_EQUAL(-2, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_rejects_zero_channels(void)
{
    mp3_test_set_format_override(1, 0, 44100);
    TEST_ASSERT_EQUAL(-4, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_rejects_too_many_channels(void)
{
    mp3_test_set_format_override(1, 3, 44100);
    TEST_ASSERT_EQUAL(-4, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_rejects_sample_rate_too_low(void)
{
    mp3_test_set_format_override(1, 2, 299);
    TEST_ASSERT_EQUAL(-4, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_rejects_sample_rate_too_high(void)
{
    mp3_test_set_format_override(1, 2, 48001);
    TEST_ASSERT_EQUAL(-4, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_accepts_boundary_sample_rates(void)
{
    /* Happy: inclusive low/high sample-rate edges (channel count from file). */
    unsigned ch;

    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    ch = host_pcm_channels();
    TEST_ASSERT_TRUE(ch == 1 || ch == 2);

    host_mock_reset();
    audio_ctrl_reset();
    mp3_test_set_format_override(1, ch, 300);
    mp3_test_set_decode_max_chunks(1);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_EQUAL_FLOAT(300.0f, host_pcm_sample_rate());
    TEST_ASSERT_EQUAL(ch, host_pcm_channels());

    host_mock_reset();
    audio_ctrl_reset();
    mp3_test_set_format_override(1, ch, 48000);
    mp3_test_set_decode_max_chunks(1);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_EQUAL_FLOAT(48000.0f, host_pcm_sample_rate());
}

static void test_decode_buf_alloc_failure(void)
{
    mp3_test_set_decode_alloc_fail(1, 0);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_pcm16_alloc_failure(void)
{
    mp3_test_set_decode_alloc_fail(0, 1);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_ring_alloc_failure(void)
{
    /* file image succeeds; ring linearAlloc fails → -5 */
    host_mock_set_alloc_fail_after(1);
    TEST_ASSERT_EQUAL(-5, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_resume_skips_into_stream(void)
{
    /* Skip past some PCM frames, then play the rest. */
    audio_note_paused_at(2000);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_GREATER_THAN(0, (int)host_pcm_samples_fed());
}

static void test_resume_skip_exhausts_stream(void)
{
    /* Huge resume target → skip loop ends via frame_bytes <= 0 / EOF. */
    audio_note_paused_at(1000000000LL);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_resume_skip_breaks_on_frame_bytes_zero(void)
{
    audio_note_paused_at(2000);
    mp3_test_set_skip_frame_bytes_zero(1);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_invalid_mp3_data(void)
{
    FILE *f = fopen("fixtures/garbage.mp3", "wb");
    TEST_ASSERT_NOT_NULL(f);
    fwrite("NOTMP3DATA", 1, 10, f);
    fclose(f);
    TEST_ASSERT_EQUAL(-2, audio_play_mp3("fixtures/garbage.mp3"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

static void test_plays_mini_fixture(void)
{
    /* Happy: full decode/playback (fixture includes an ID3v2 tag). */
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_GREATER_THAN(4096, (int)host_pcm_samples_fed());
    TEST_ASSERT_GREATER_THAN(1, (int)host_pcm_buffer_submissions());
    TEST_ASSERT_TRUE(host_pcm_channels() == 1 || host_pcm_channels() == 2);
    TEST_ASSERT_TRUE(host_pcm_sample_rate() >= 300.0f && host_pcm_sample_rate() <= 48000.0f);
    /* Non-aligned length → partial final wavebuf */
    if ((host_pcm_samples_fed() % 4096u) != 0)
        TEST_ASSERT_LESS_THAN(4096, (int)host_pcm_last_chunk_samples());
    /* Natural end: duration snapped to played total → seek ratio is 1.0. */
    TEST_ASSERT_EQUAL_INT64((int64_t)host_pcm_samples_fed(),
                            audio_ctrl_duration_samples());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, audio_progress_ratio());
}

static void test_decode_yields_when_ring_full(void)
{
    mp3_test_set_fake_producer_full(2);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_GREATER_THAN(0, (int)host_pcm_samples_fed());
}

static void test_decode_breaks_on_frame_bytes_zero(void)
{
    /* Probe still uses real frames; decode_thread bails on first frame. */
    mp3_test_set_decode_frame_bytes_zero(1);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_decode_exits_via_should_stop(void)
{
    mp3_test_set_decode_spin(1);
    mp3_test_set_decode_spin_stop_after(3);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    mp3_test_set_decode_spin(0);
}

static void test_wavebuf_fail_stops_spinning_decode(void)
{
    mp3_test_set_decode_spin(1);
    /* file + ring + decode_buf + pcm16; fail first wavebuf after decode is in-loop */
    host_mock_set_alloc_fail_after(4);
    TEST_ASSERT_EQUAL(-5, audio_play_mp3("fixtures/mini.mp3"));
    mp3_test_set_decode_spin(0);
}

static void test_decode_spin_wait_exits_on_decode_done(void)
{
    /* Spin wait: decode OOM → decode_done without g_decode_in_loop. */
    mp3_test_set_decode_spin(1);
    mp3_test_set_decode_alloc_fail(1, 0);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    mp3_test_set_decode_spin(0);
}

static void test_wavebuf_alloc_failure_frees_prior(void)
{
    /* First wavebuf OK, second fails → free bufs[0] (i > 0 cleanup). */
    mp3_test_set_decode_spin(1);
    host_mock_set_alloc_fail_after(5); /* file+ring+decode+pcm16+1 wb */
    TEST_ASSERT_EQUAL(-5, audio_play_mp3("fixtures/mini.mp3"));
    mp3_test_set_decode_spin(0);
}

static void test_pause_notes_resume_point(void)
{
    /* Pause requested before play → prefill/main exit via should_exit. */
    audio_ctrl_test_set_playing(1);
    audio_pause();
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_EQUAL_INT64(0, audio_take_resume_sample());
}

static void test_prefill_exits_on_decode_done(void)
{
    /* Decode finishes with little PCM → break prefill via decode_done. */
    mp3_test_set_decode_frame_bytes_zero(1);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_playback_waits_for_data(void)
{
    /* Short resume prefill so playback starts while decode is still throttled. */
    audio_note_paused_at(0);
    mp3_test_set_decode_throttle(50, 5000000); /* 50 × 5 ms */
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_GREATER_THAN(0, (int)host_pcm_samples_fed());
}

static void test_decode_throttle_zero_ns_skips_sleep(void)
{
    /* times > 0 with ns == 0 → take the false arm of g_decode_throttle_ns > 0. */
    mp3_test_set_decode_throttle(8, 0);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
}

static void test_playback_waits_on_busy_wavebuf(void)
{
    /* Hold buf0; more than 4 chunks so wrap hits PLAYING with data left. */
    host_mock_set_wavebuf_hold(1);
    mp3_test_set_decode_max_chunks(5);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_GREATER_THAN(0, (int)host_pcm_samples_fed());
}

static void test_playback_busy_while_decoding(void)
{
    /* Large (non-resume) prefill so bufs 0–3 fill without PLAYBACK_YIELD,
     * which would release the held buf0 before next wraps. Throttle keeps
     * decode_done false on the busy path. */
    host_mock_set_wavebuf_hold(1);
    mp3_test_set_decode_throttle(300, 3000000);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_GREATER_THAN(0, (int)host_pcm_samples_fed());
}

static void test_playback_ends_when_busy_and_ring_empty(void)
{
    /* Exact 4 chunks: wrap to held buf0 with decode_done && empty ring. */
    host_mock_set_wavebuf_hold(1);
    host_mock_set_wavebuf_persist(1);
    mp3_test_set_decode_max_chunks(4);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_EQUAL(16384, (int)host_pcm_samples_fed());
}

static void test_playback_ends_exact_empty_on_free_buf(void)
{
    /* Full chunks only → decode_done with avail==0 (no partial pop). */
    mp3_test_set_decode_max_chunks(2);
    TEST_ASSERT_EQUAL(0, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_EQUAL(8192, (int)host_pcm_samples_fed());
}

static void test_thread_create_failure(void)
{
    host_mock_set_thread_fail(1);
    TEST_ASSERT_EQUAL(-7, audio_play_mp3("fixtures/mini.mp3"));
    TEST_ASSERT_EQUAL(0, host_pcm_samples_fed());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_missing_file);
    RUN_TEST(test_empty_file);
    RUN_TEST(test_fseek_failure);
    RUN_TEST(test_file_buf_alloc_failure);
    RUN_TEST(test_fread_failure);
    RUN_TEST(test_probe_frame_bytes_zero);
    RUN_TEST(test_probe_partial_frame_info);
    RUN_TEST(test_probe_step_overflow_ignored);
    RUN_TEST(test_probe_exhausts_injected_steps);
    RUN_TEST(test_rejects_zero_channels);
    RUN_TEST(test_rejects_too_many_channels);
    RUN_TEST(test_rejects_sample_rate_too_low);
    RUN_TEST(test_rejects_sample_rate_too_high);
    RUN_TEST(test_accepts_boundary_sample_rates);
    RUN_TEST(test_decode_buf_alloc_failure);
    RUN_TEST(test_pcm16_alloc_failure);
    RUN_TEST(test_ring_alloc_failure);
    RUN_TEST(test_resume_skips_into_stream);
    RUN_TEST(test_resume_skip_exhausts_stream);
    RUN_TEST(test_resume_skip_breaks_on_frame_bytes_zero);
    RUN_TEST(test_invalid_mp3_data);
    RUN_TEST(test_plays_mini_fixture);
    RUN_TEST(test_decode_yields_when_ring_full);
    RUN_TEST(test_decode_breaks_on_frame_bytes_zero);
    RUN_TEST(test_decode_exits_via_should_stop);
    RUN_TEST(test_wavebuf_fail_stops_spinning_decode);
    RUN_TEST(test_decode_spin_wait_exits_on_decode_done);
    RUN_TEST(test_wavebuf_alloc_failure_frees_prior);
    RUN_TEST(test_pause_notes_resume_point);
    RUN_TEST(test_prefill_exits_on_decode_done);
    RUN_TEST(test_playback_waits_for_data);
    RUN_TEST(test_decode_throttle_zero_ns_skips_sleep);
    RUN_TEST(test_playback_waits_on_busy_wavebuf);
    RUN_TEST(test_playback_busy_while_decoding);
    RUN_TEST(test_playback_ends_when_busy_and_ring_empty);
    RUN_TEST(test_playback_ends_exact_empty_on_free_buf);
    RUN_TEST(test_thread_create_failure);
    return UNITY_END();
}
