/* Minimal audio playback using vgmstream + 3DS NDSP.
 * Supports all formats vgmstream supports (WAV, BRSTM, ADPCM, OGG, etc.) with no optional libs. */

#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include "audio.h"
#include "audio_ctrl.h"
#include "audio_ctrl_internal.h"
#include "file_magic.h"
#include "libvgmstream.h"

#define SAMPLES_PER_BUF  (1600)
#define N_WAVEBUFS       (4)
#define PLAYBACK_YIELD_US  (8000)

static bool g_audio_initialized = false;
static char g_play_path[256];

static void wait_playback_idle(void)
{
    audio_stop();
    while (audio_is_playing())
        svcSleepThread(100000);
    audio_ctrl_after_stop_wait();
}

void audio_stop_wait(void)
{
    wait_playback_idle();
    g_play_path[0] = '\0';
}

int audio_init(void)
{
    if (g_audio_initialized)
        return 0;
    if (ndspInit() != 0)
        return -1;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    g_audio_initialized = true;
    return 0;
}

void audio_exit(void)
{
    if (!g_audio_initialized)
        return;
    wait_playback_idle();
    ndspExit();
    g_audio_initialized = false;
}

static void playback_thread_func(void *arg)
{
    int result;

    (void)arg;
    audio_ctrl_on_playback_start();
    result = audio_play_file(g_play_path);
    audio_ctrl_on_playback_end(result, audio_should_stop());
}

static int start_playback_thread(int keep_resume)
{
    audio_ctrl_prepare_async(keep_resume);

    if (!threadCreate(playback_thread_func, NULL, 0x8000, 0x30, -1, true))
        return -7;
    return 0;
}

void audio_resume(void)
{
    if (!audio_is_paused() || audio_is_playing())
        return;
    audio_ctrl_set_paused_flag(0);
    (void)start_playback_thread(1);
}

int audio_seek_ratio(float ratio)
{
    int64_t duration;
    int64_t sample;
    int     was_playing;
    int     was_paused;

    was_playing = audio_is_playing();
    was_paused  = audio_is_paused();
    if (!was_playing && !was_paused)
        return -1;

    duration = audio_ctrl_duration_samples();
    if (duration <= 0)
        return -1;

    if (ratio < 0.f)
        ratio = 0.f;
    if (ratio > 1.f)
        ratio = 1.f;

    sample = (int64_t)((double)ratio * (double)duration);
    if (sample < 0)
        sample = 0;
    if (sample > duration)
        sample = duration;

    if (was_playing) {
        /* audio_stop() clears resume — apply seek target after the thread exits. */
        audio_stop();
        while (audio_is_playing())
            svcSleepThread(100000);
        audio_ctrl_after_seek_restart();
        audio_ctrl_set_resume_sample(sample);
        if (start_playback_thread(1) != 0)
            return -1;
        return 0;
    }

    /* Paused: update resume only; stay paused. */
    audio_ctrl_set_resume_sample(sample);
    audio_ctrl_set_paused_flag(1);
    return 0;
}

const char *audio_current_path(void)
{
    if (g_play_path[0] == '\0')
        return NULL;
    if (!audio_is_playing() && !audio_is_paused())
        return NULL;
    return g_play_path;
}

const char *audio_last_path(void)
{
    if (g_play_path[0] == '\0')
        return NULL;
    return g_play_path;
}

int audio_play_file_async(const char *path)
{
    int r;

    if (!g_audio_initialized || !path) {
        audio_set_play_error(-1);
        return -1;
    }

    if (audio_is_playing())
        wait_playback_idle();

    audio_clear_play_error();
    audio_ctrl_set_paused_flag(0);
    audio_ctrl_clear_timeline();
    strncpy(g_play_path, path, sizeof(g_play_path) - 1);
    g_play_path[sizeof(g_play_path) - 1] = '\0';

    r = start_playback_thread(0);
    if (r != 0)
        audio_set_play_error(r);
    return r;
}

static ndspWaveBuf g_waveBufs[N_WAVEBUFS];
static int g_channel_used = -1;

int audio_play_file(const char *path)
{
    if (!path || !g_audio_initialized)
        return -1;

    switch (audio_route_for_path(path)) {
    case AUDIO_ROUTE_FLAC:
        return audio_play_flac(path);
    case AUDIO_ROUTE_MP3:
        return audio_play_mp3(path);
    default:
        break;
    }

    audio_ctrl_clear_exit_flags();

    libstreamfile_t *libsf = libstreamfile_open_from_stdio(path);
    if (!libsf)
        return -2;

    libvgmstream_config_t cfg = {0};
    cfg.allow_play_forever = false;
    libvgmstream_t *vg = libvgmstream_create(libsf, 0, &cfg);
    libstreamfile_close(libsf);
    if (!vg)
        return -3;

    {
        int64_t resume = audio_take_resume_sample();
        if (resume >= 0) {
            libvgmstream_seek(vg, resume);
            audio_ctrl_set_position(resume);
        }
    }

    const libvgmstream_format_t *fmt = vg->format;
    int ch = fmt->channels;
    int sr = fmt->sample_rate;
    if (ch < 1 || ch > 2 || sr < 300 || sr > 48000) {
        libvgmstream_free(vg);
        return -4;
    }

    {
        int64_t dur = fmt->play_samples;
        if (dur <= 0)
            dur = fmt->stream_samples;
        audio_ctrl_set_duration(dur);
    }

    int ndsp_format = (ch == 2) ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16;
    int channel_id = 0;
    ndspChnReset(channel_id);
    ndspChnSetInterp(channel_id, NDSP_INTERP_LINEAR);
    ndspChnSetRate(channel_id, (float)sr);
    ndspChnSetFormat(channel_id, ndsp_format);
    ndspChnSetMix(channel_id, (float[12]){ 1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
    g_channel_used = channel_id;

    size_t buf_samples = SAMPLES_PER_BUF * ch;
    size_t buf_bytes   = buf_samples * 2;
    void *bufs[N_WAVEBUFS];
    for (int i = 0; i < N_WAVEBUFS; i++) {
        bufs[i] = linearAlloc(buf_bytes);
        if (!bufs[i]) {
            for (int j = 0; j < i; j++)
                linearFree(bufs[j]);
            libvgmstream_free(vg);
            return -5;
        }
        memset(&g_waveBufs[i], 0, sizeof(ndspWaveBuf));
        g_waveBufs[i].data_vaddr = bufs[i];
        g_waveBufs[i].nsamples   = SAMPLES_PER_BUF;
        g_waveBufs[i].status      = NDSP_WBUF_FREE;
    }

    int next_buf = 0;
    int ret = 0;
    bool stream_done = false;

    while (!audio_playback_should_exit()) {
        bool progressed = false;
        if (!stream_done) {
            ndspWaveBuf *wb = &g_waveBufs[next_buf];
            if (wb->status == NDSP_WBUF_FREE || wb->status == NDSP_WBUF_DONE) {
                int got = libvgmstream_fill(vg, bufs[next_buf], SAMPLES_PER_BUF);
                if (got <= 0) {
                    if (vg->decoder->done)
                        stream_done = true;
                } else {
                    wb->nsamples = got;
                    wb->status   = NDSP_WBUF_FREE;
                    DSP_FlushDataCache(bufs[next_buf], buf_bytes);
                    ndspChnWaveBufAdd(channel_id, wb);
                    next_buf = (next_buf + 1) % N_WAVEBUFS;
                    progressed = true;
                    audio_ctrl_set_position(libvgmstream_get_play_position(vg));
                }
            }
        }
        if (stream_done) {
            bool all_done = true;
            for (int i = 0; i < N_WAVEBUFS; i++)
                if (g_waveBufs[i].status == NDSP_WBUF_PLAYING)
                    all_done = false;
            if (all_done)
                break;
        }
        if (!progressed)
            svcSleepThread(PLAYBACK_YIELD_US);
    }

    if (audio_end_is_pause())
        audio_note_paused_at(libvgmstream_get_play_position(vg));

    ndspChnWaveBufClear(channel_id);
    for (int i = 0; i < N_WAVEBUFS; i++)
        linearFree(bufs[i]);
    libvgmstream_free(vg);
    g_channel_used = -1;
    return ret;
}
