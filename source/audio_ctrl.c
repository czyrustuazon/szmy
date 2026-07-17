#include "audio_ctrl.h"
#include "audio_ctrl_internal.h"

#include <stdbool.h>

static volatile bool g_stop_requested;
static volatile bool g_pause_requested;
static volatile bool g_playing;
static volatile bool g_ended_naturally;
static bool          g_paused;
static bool          g_have_resume;
static int64_t       g_resume_sample;
static volatile int  g_last_play_error;
static volatile int64_t g_duration_samples;
static volatile int64_t g_position_samples;

void audio_ctrl_clear_timeline(void)
{
    g_duration_samples = 0;
    g_position_samples = 0;
}

void audio_ctrl_reset(void)
{
    g_stop_requested   = false;
    g_pause_requested  = false;
    g_playing          = false;
    g_ended_naturally  = false;
    g_paused           = false;
    g_have_resume      = false;
    g_resume_sample    = 0;
    g_last_play_error  = 0;
    audio_ctrl_clear_timeline();
}

void audio_stop(void)
{
    g_stop_requested  = true;
    g_pause_requested = false;
    g_paused          = false;
    g_have_resume     = false;
    g_ended_naturally = false;
}

void audio_pause(void)
{
    if (!g_playing)
        return;
    g_pause_requested = true;
}

int audio_should_stop(void)
{
    return g_stop_requested;
}

int audio_playback_should_exit(void)
{
    return g_stop_requested || g_pause_requested;
}

int64_t audio_take_resume_sample(void)
{
    if (!g_have_resume)
        return -1;
    g_have_resume = false;
    return g_resume_sample;
}

void audio_note_paused_at(int64_t sample)
{
    g_resume_sample   = sample;
    g_have_resume     = true;
    g_paused          = true;
    g_pause_requested = false;
    g_position_samples = sample;
}

void audio_ctrl_set_resume_sample(int64_t sample)
{
    if (sample < 0)
        sample = 0;
    g_resume_sample    = sample;
    g_have_resume      = true;
    g_position_samples = sample;
}

void audio_ctrl_set_duration(int64_t samples)
{
    if (samples < 0)
        samples = 0;
    g_duration_samples = samples;
}

void audio_ctrl_set_position(int64_t samples)
{
    if (samples < 0)
        samples = 0;
    if (g_duration_samples > 0 && samples > g_duration_samples)
        samples = g_duration_samples;
    g_position_samples = samples;
}

int64_t audio_ctrl_duration_samples(void)
{
    return g_duration_samples;
}

int64_t audio_ctrl_position_samples(void)
{
    return g_position_samples;
}

float audio_progress_ratio(void)
{
    int64_t dur = g_duration_samples;
    int64_t pos = g_position_samples;

    if (dur <= 0)
        return 0.f;
    if (pos <= 0)
        return 0.f;
    if (pos >= dur)
        return 1.f;
    return (float)((double)pos / (double)dur);
}

int audio_end_is_pause(void)
{
    return g_pause_requested && !g_stop_requested;
}

int audio_is_playing(void)
{
    return g_playing;
}

int audio_is_paused(void)
{
    return g_paused && !g_playing;
}

int audio_last_play_error(void)
{
    return g_last_play_error;
}

void audio_set_play_error(int error)
{
    g_last_play_error = error;
}

void audio_clear_play_error(void)
{
    g_last_play_error = 0;
}

#ifdef UNIT_TEST
void audio_ctrl_test_set_playing(int playing)
{
    g_playing = playing ? true : false;
}

void audio_ctrl_test_set_paused(int paused)
{
    g_paused = paused ? true : false;
}

void audio_ctrl_test_set_request_flags(int stop, int pause)
{
    g_stop_requested  = stop ? true : false;
    g_pause_requested = pause ? true : false;
}
#endif

/* Used by audio.c playback thread — not part of public audio.h API. */
void audio_ctrl_on_playback_start(void)
{
    g_playing         = true;
    g_stop_requested  = false;
    g_pause_requested = false;
    g_ended_naturally = false;
}

void audio_ctrl_on_playback_end(int result, int was_stopped)
{
    if (result != 0 && !g_stop_requested)
        audio_set_play_error(result);
    else if (result == 0 && !g_paused)
        audio_clear_play_error();
    if (!g_paused)
        g_have_resume = false;
    /* Natural end: finished decoding, not stop/pause. */
    if (result == 0 && !was_stopped && !g_paused) {
        g_ended_naturally = true;
        if (g_duration_samples > 0)
            g_position_samples = g_duration_samples;
    }
    g_playing = false;
}

int audio_consume_ended_naturally(void)
{
    if (!g_ended_naturally)
        return 0;
    g_ended_naturally = 0;
    return 1;
}

void audio_ctrl_prepare_async(int keep_resume)
{
    if (!keep_resume)
        g_have_resume = false;
    g_stop_requested  = false;
    g_pause_requested = false;
}

void audio_ctrl_after_stop_wait(void)
{
    g_paused          = false;
    g_have_resume     = false;
    g_ended_naturally = false;
    audio_ctrl_clear_timeline();
}

void audio_ctrl_after_seek_restart(void)
{
    /* Keep resume sample + timeline; clear stop/pause and paused flag. */
    g_stop_requested  = false;
    g_pause_requested = false;
    g_paused          = false;
    g_ended_naturally = false;
}

void audio_ctrl_clear_exit_flags(void)
{
    g_stop_requested  = false;
    g_pause_requested = false;
}

void audio_ctrl_set_paused_flag(int paused)
{
    g_paused = paused ? true : false;
}

void audio_ctrl_set_playing_flag(int playing)
{
    g_playing = playing ? true : false;
}
