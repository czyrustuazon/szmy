#include "audio_ctrl.h"

#include <stdbool.h>

static volatile bool g_stop_requested;
static volatile bool g_pause_requested;
static volatile bool g_playing;
static bool          g_paused;
static bool          g_have_resume;
static int64_t       g_resume_sample;
static volatile int  g_last_play_error;

void audio_ctrl_reset(void)
{
    g_stop_requested   = false;
    g_pause_requested  = false;
    g_playing          = false;
    g_paused           = false;
    g_have_resume      = false;
    g_resume_sample    = 0;
    g_last_play_error  = 0;
}

void audio_stop(void)
{
    g_stop_requested  = true;
    g_pause_requested = false;
    g_paused          = false;
    g_have_resume     = false;
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
}

void audio_ctrl_on_playback_end(int result, int was_stopped)
{
    (void)was_stopped;
    if (result != 0 && !g_stop_requested)
        audio_set_play_error(result);
    else if (result == 0 && !g_paused)
        audio_clear_play_error();
    if (!g_paused)
        g_have_resume = false;
    g_playing = false;
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
    g_paused      = false;
    g_have_resume = false;
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
