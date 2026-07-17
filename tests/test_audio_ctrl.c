#include "unity.h"
#include "audio.h"
#include "audio_ctrl.h"
#include "audio_ctrl_internal.h"

void setUp(void) { audio_ctrl_reset(); }
void tearDown(void) {}

/* --- Idle / reset (baseline happy path) --- */

static void test_idle_defaults_after_reset(void)
{
    TEST_ASSERT_FALSE(audio_should_stop());
    TEST_ASSERT_FALSE(audio_playback_should_exit());
    TEST_ASSERT_FALSE(audio_end_is_pause());
    TEST_ASSERT_FALSE(audio_is_playing());
    TEST_ASSERT_FALSE(audio_is_paused());
    TEST_ASSERT_EQUAL_INT(0, audio_last_play_error());
    TEST_ASSERT_EQUAL_INT64(-1, audio_take_resume_sample());
}

/* --- Stop --- */

static void test_stop_and_should_stop(void)
{
    TEST_ASSERT_FALSE(audio_should_stop());
    audio_stop();
    TEST_ASSERT_TRUE(audio_should_stop());
    TEST_ASSERT_TRUE(audio_playback_should_exit());
}

static void test_stop_clears_pause_and_resume(void)
{
    audio_note_paused_at(8000);
    TEST_ASSERT_TRUE(audio_is_paused());
    audio_ctrl_test_set_playing(1);
    audio_pause();
    TEST_ASSERT_TRUE(audio_playback_should_exit());

    audio_stop();
    TEST_ASSERT_TRUE(audio_should_stop());
    TEST_ASSERT_FALSE(audio_end_is_pause());
    TEST_ASSERT_FALSE(audio_is_paused());
    TEST_ASSERT_EQUAL_INT64(-1, audio_take_resume_sample());
}

/* --- Pause --- */

static void test_pause_while_playing(void)
{
    audio_ctrl_test_set_playing(1);
    audio_pause();
    TEST_ASSERT_TRUE(audio_end_is_pause());
    TEST_ASSERT_TRUE(audio_playback_should_exit());
}

static void test_pause_ignored_when_idle(void)
{
    audio_pause();
    TEST_ASSERT_FALSE(audio_end_is_pause());
    TEST_ASSERT_FALSE(audio_playback_should_exit());
}

/* --- Resume sample --- */

static void test_resume_sample_roundtrip(void)
{
    TEST_ASSERT_EQUAL_INT64(-1, audio_take_resume_sample());
    audio_note_paused_at(44100);
    TEST_ASSERT_TRUE(audio_is_paused());
    TEST_ASSERT_EQUAL_INT64(44100, audio_take_resume_sample());
    TEST_ASSERT_EQUAL_INT64(-1, audio_take_resume_sample());
}

/* --- Play error storage --- */

static void test_play_error_storage(void)
{
    audio_set_play_error(-3);
    TEST_ASSERT_EQUAL_INT(-3, audio_last_play_error());
    audio_clear_play_error();
    TEST_ASSERT_EQUAL_INT(0, audio_last_play_error());
}

/* --- Test helpers / flag setters --- */

static void test_set_paused_helper(void)
{
    audio_ctrl_test_set_paused(1);
    TEST_ASSERT_TRUE(audio_is_paused());
    audio_ctrl_test_set_paused(0);
    TEST_ASSERT_FALSE(audio_is_paused());
}

static void test_set_flags(void)
{
    audio_ctrl_set_playing_flag(1);
    TEST_ASSERT_TRUE(audio_is_playing());
    audio_ctrl_set_playing_flag(0);
    TEST_ASSERT_FALSE(audio_is_playing());

    audio_ctrl_set_paused_flag(1);
    TEST_ASSERT_TRUE(audio_is_paused());
    audio_ctrl_set_paused_flag(0);
    TEST_ASSERT_FALSE(audio_is_paused());
}

/* --- Playback lifecycle --- */

static void test_playback_start_clears_exit_flags(void)
{
    audio_stop();
    audio_ctrl_test_set_playing(1);
    audio_pause();
    TEST_ASSERT_TRUE(audio_should_stop());
    TEST_ASSERT_TRUE(audio_playback_should_exit());

    audio_ctrl_on_playback_start();
    TEST_ASSERT_TRUE(audio_is_playing());
    TEST_ASSERT_FALSE(audio_should_stop());
    TEST_ASSERT_FALSE(audio_playback_should_exit());
}

static void test_playback_end_sets_error_when_not_stopped(void)
{
    audio_ctrl_on_playback_start();
    audio_ctrl_on_playback_end(-5, 0);
    TEST_ASSERT_FALSE(audio_is_playing());
    TEST_ASSERT_EQUAL_INT(-5, audio_last_play_error());
}

static void test_playback_end_ignores_error_when_stopped(void)
{
    audio_ctrl_on_playback_start();
    audio_set_play_error(0);
    audio_stop();
    audio_ctrl_on_playback_end(-5, 1);
    TEST_ASSERT_EQUAL_INT(0, audio_last_play_error());
    TEST_ASSERT_FALSE(audio_is_playing());
}

static void test_playback_end_success_clears_error_and_resume(void)
{
    audio_set_play_error(-2);
    audio_note_paused_at(100);
    audio_ctrl_set_paused_flag(0); /* natural end, not a pause */
    audio_ctrl_on_playback_start();
    audio_ctrl_on_playback_end(0, 0);
    TEST_ASSERT_EQUAL_INT(0, audio_last_play_error());
    TEST_ASSERT_EQUAL_INT64(-1, audio_take_resume_sample());
}

static void test_playback_end_while_paused_keeps_resume(void)
{
    audio_set_play_error(-4);
    audio_note_paused_at(22050);
    audio_ctrl_on_playback_start();
    audio_ctrl_set_paused_flag(1);
    audio_ctrl_on_playback_end(0, 0);
    /* success while paused: error not cleared; resume sample kept */
    TEST_ASSERT_EQUAL_INT(-4, audio_last_play_error());
    TEST_ASSERT_EQUAL_INT64(22050, audio_take_resume_sample());
}

static void test_playback_end_error_while_paused_keeps_resume(void)
{
    audio_note_paused_at(11025);
    audio_ctrl_on_playback_start();
    audio_ctrl_set_paused_flag(1);
    audio_ctrl_on_playback_end(-7, 0);
    TEST_ASSERT_EQUAL_INT(-7, audio_last_play_error());
    TEST_ASSERT_EQUAL_INT64(11025, audio_take_resume_sample());
    TEST_ASSERT_FALSE(audio_is_playing());
}

/* --- prepare_async / after_stop_wait / clear_exit_flags --- */

static void test_prepare_async_keep_and_drop_resume(void)
{
    /* Do not call audio_stop() here — it clears the resume sample. */
    audio_note_paused_at(99);
    audio_ctrl_test_set_playing(1);
    audio_pause();
    TEST_ASSERT_TRUE(audio_playback_should_exit());

    audio_ctrl_prepare_async(1);
    TEST_ASSERT_FALSE(audio_playback_should_exit());
    TEST_ASSERT_EQUAL_INT64(99, audio_take_resume_sample());

    audio_note_paused_at(50);
    audio_ctrl_prepare_async(0);
    TEST_ASSERT_EQUAL_INT64(-1, audio_take_resume_sample());
}

static void test_after_stop_wait_clears_pause_state(void)
{
    audio_note_paused_at(1234);
    TEST_ASSERT_TRUE(audio_is_paused());
    audio_ctrl_after_stop_wait();
    TEST_ASSERT_FALSE(audio_is_paused());
    TEST_ASSERT_EQUAL_INT64(-1, audio_take_resume_sample());
}

static void test_clear_exit_flags(void)
{
    audio_stop();
    audio_ctrl_test_set_playing(1);
    audio_pause();
    audio_ctrl_clear_exit_flags();
    TEST_ASSERT_FALSE(audio_should_stop());
    TEST_ASSERT_FALSE(audio_playback_should_exit());
}

/* --- Compound query edge cases --- */

static void test_end_is_pause_false_when_also_stopped(void)
{
    /* audio_stop() clears pause_requested; set both flags directly. */
    audio_ctrl_test_set_request_flags(1, 1);
    TEST_ASSERT_FALSE(audio_end_is_pause());
}

static void test_is_paused_false_while_still_playing(void)
{
    audio_ctrl_test_set_paused(1);
    audio_ctrl_test_set_playing(1);
    TEST_ASSERT_FALSE(audio_is_paused());
}

/* --- Natural end / auto-advance flag --- */

static void test_ended_naturally_happy_consume(void)
{
    /* Happy: track finishes → flag set → consume once → clear */
    TEST_ASSERT_FALSE(audio_consume_ended_naturally());
    audio_ctrl_on_playback_start();
    audio_ctrl_on_playback_end(0, 0);
    TEST_ASSERT_TRUE(audio_consume_ended_naturally());
    TEST_ASSERT_FALSE(audio_consume_ended_naturally());
}

static void test_ended_naturally_sad_stop_pause_error(void)
{
    /* Sad: stop does not count as natural end */
    audio_ctrl_on_playback_start();
    audio_stop();
    audio_ctrl_on_playback_end(0, 1);
    TEST_ASSERT_FALSE(audio_consume_ended_naturally());

    /* Sad: pause end does not count */
    audio_ctrl_reset();
    audio_ctrl_on_playback_start();
    audio_ctrl_set_paused_flag(1);
    audio_ctrl_on_playback_end(0, 0);
    TEST_ASSERT_FALSE(audio_consume_ended_naturally());

    /* Sad: error end does not count */
    audio_ctrl_reset();
    audio_ctrl_on_playback_start();
    audio_ctrl_on_playback_end(-3, 0);
    TEST_ASSERT_FALSE(audio_consume_ended_naturally());
}

static void test_ended_naturally_cleared_on_start_stop_wait(void)
{
    /* Flag set then cleared by a new start */
    audio_ctrl_on_playback_start();
    audio_ctrl_on_playback_end(0, 0);
    audio_ctrl_on_playback_start();
    TEST_ASSERT_FALSE(audio_consume_ended_naturally());

    /* Flag cleared by stop */
    audio_ctrl_on_playback_start();
    audio_ctrl_on_playback_end(0, 0);
    audio_stop();
    TEST_ASSERT_FALSE(audio_consume_ended_naturally());

    /* Flag cleared by after_stop_wait */
    audio_ctrl_reset();
    audio_ctrl_on_playback_start();
    audio_ctrl_on_playback_end(0, 0);
    audio_ctrl_after_stop_wait();
    TEST_ASSERT_FALSE(audio_consume_ended_naturally());
}

/* --- Duration / position / seek timeline --- */

static void test_duration_position_and_progress_ratio(void)
{
    TEST_ASSERT_EQUAL_INT64(0, audio_ctrl_duration_samples());
    TEST_ASSERT_EQUAL_INT64(0, audio_ctrl_position_samples());
    TEST_ASSERT_EQUAL_FLOAT(0.f, audio_progress_ratio());

    audio_ctrl_set_duration(-10);
    TEST_ASSERT_EQUAL_INT64(0, audio_ctrl_duration_samples());
    TEST_ASSERT_EQUAL_FLOAT(0.f, audio_progress_ratio());

    audio_ctrl_set_duration(1000);
    TEST_ASSERT_EQUAL_INT64(1000, audio_ctrl_duration_samples());
    TEST_ASSERT_EQUAL_FLOAT(0.f, audio_progress_ratio()); /* position still 0 */

    audio_ctrl_set_position(-5);
    TEST_ASSERT_EQUAL_INT64(0, audio_ctrl_position_samples());
    TEST_ASSERT_EQUAL_FLOAT(0.f, audio_progress_ratio());

    audio_ctrl_set_position(250);
    TEST_ASSERT_EQUAL_INT64(250, audio_ctrl_position_samples());
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, audio_progress_ratio());

    audio_ctrl_set_position(2000); /* clamp to duration */
    TEST_ASSERT_EQUAL_INT64(1000, audio_ctrl_position_samples());
    TEST_ASSERT_EQUAL_FLOAT(1.f, audio_progress_ratio());

    audio_ctrl_clear_timeline();
    TEST_ASSERT_EQUAL_INT64(0, audio_ctrl_duration_samples());
    TEST_ASSERT_EQUAL_INT64(0, audio_ctrl_position_samples());
}

static void test_set_resume_sample_sets_position(void)
{
    audio_ctrl_set_duration(5000);
    audio_ctrl_set_resume_sample(-3);
    TEST_ASSERT_EQUAL_INT64(0, audio_ctrl_position_samples());
    TEST_ASSERT_EQUAL_INT64(0, audio_take_resume_sample());

    audio_ctrl_set_resume_sample(1234);
    TEST_ASSERT_EQUAL_INT64(1234, audio_ctrl_position_samples());
    TEST_ASSERT_EQUAL_INT64(1234, audio_take_resume_sample());
    TEST_ASSERT_EQUAL_INT64(-1, audio_take_resume_sample());
}

static void test_after_seek_restart_preserves_resume_and_timeline(void)
{
    /* Unlike after_stop_wait, seek restart must not drop resume/timeline.
     * (audio_stop clears resume; seek re-applies it after this helper.) */
    audio_ctrl_set_duration(9000);
    audio_ctrl_set_resume_sample(4500);
    audio_ctrl_test_set_paused(1);
    audio_ctrl_test_set_request_flags(1, 1);

    audio_ctrl_after_seek_restart();

    TEST_ASSERT_FALSE(audio_should_stop());
    TEST_ASSERT_FALSE(audio_playback_should_exit());
    TEST_ASSERT_FALSE(audio_is_paused());
    TEST_ASSERT_EQUAL_INT64(9000, audio_ctrl_duration_samples());
    TEST_ASSERT_EQUAL_INT64(4500, audio_ctrl_position_samples());
    TEST_ASSERT_EQUAL_INT64(4500, audio_take_resume_sample());
}

static void test_natural_end_snaps_position_to_duration(void)
{
    audio_ctrl_set_duration(800);
    audio_ctrl_set_position(100);
    audio_ctrl_on_playback_start();
    audio_ctrl_on_playback_end(0, 0);
    TEST_ASSERT_EQUAL_INT64(800, audio_ctrl_position_samples());
    TEST_ASSERT_EQUAL_FLOAT(1.f, audio_progress_ratio());
    TEST_ASSERT_TRUE(audio_consume_ended_naturally());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_idle_defaults_after_reset);
    RUN_TEST(test_stop_and_should_stop);
    RUN_TEST(test_stop_clears_pause_and_resume);
    RUN_TEST(test_pause_while_playing);
    RUN_TEST(test_pause_ignored_when_idle);
    RUN_TEST(test_resume_sample_roundtrip);
    RUN_TEST(test_play_error_storage);
    RUN_TEST(test_set_paused_helper);
    RUN_TEST(test_set_flags);
    RUN_TEST(test_playback_start_clears_exit_flags);
    RUN_TEST(test_playback_end_sets_error_when_not_stopped);
    RUN_TEST(test_playback_end_ignores_error_when_stopped);
    RUN_TEST(test_playback_end_success_clears_error_and_resume);
    RUN_TEST(test_playback_end_while_paused_keeps_resume);
    RUN_TEST(test_playback_end_error_while_paused_keeps_resume);
    RUN_TEST(test_prepare_async_keep_and_drop_resume);
    RUN_TEST(test_after_stop_wait_clears_pause_state);
    RUN_TEST(test_clear_exit_flags);
    RUN_TEST(test_end_is_pause_false_when_also_stopped);
    RUN_TEST(test_is_paused_false_while_still_playing);
    RUN_TEST(test_ended_naturally_happy_consume);
    RUN_TEST(test_ended_naturally_sad_stop_pause_error);
    RUN_TEST(test_ended_naturally_cleared_on_start_stop_wait);
    RUN_TEST(test_duration_position_and_progress_ratio);
    RUN_TEST(test_set_resume_sample_sets_position);
    RUN_TEST(test_after_seek_restart_preserves_resume_and_timeline);
    RUN_TEST(test_natural_end_snaps_position_to_duration);
    return UNITY_END();
}
