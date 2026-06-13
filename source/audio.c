/* Minimal audio playback using vgmstream + 3DS NDSP.
 * Supports all formats vgmstream supports (WAV, BRSTM, ADPCM, OGG, etc.) with no optional libs. */

#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include "audio.h"
#include "libvgmstream.h"

/* FLAC magic: "fLaC" at offset 0 */
#define FLAC_MAGIC "fLaC"

#define SAMPLES_PER_BUF  (1600)
#define N_WAVEBUFS       (4)
#define PLAYBACK_YIELD_US  (8000)

static bool g_audio_initialized = false;
static volatile bool g_stop_requested = false;
static volatile bool g_pause_requested = false;
static volatile bool g_playing = false;
static bool g_paused = false;
static bool g_have_resume = false;
static char g_play_path[256];
static int64_t g_resume_sample = 0;
static volatile int g_last_play_error = 0;

const char *audio_error_message(int error)
{
    switch (error) {
    case 0:
        return NULL;
    case -1:
        return "Cannot open file";
    case -2:
        return "Cannot read audio stream";
    case -3:
        return "Unsupported format";
    case -4:
        return "Unsupported channels/sample rate";
    case -5:
        return "Out of memory";
    case -7:
        return "Could not start playback";
    default:
        return "Playback failed";
    }
}

int audio_last_play_error(void)
{
    return g_last_play_error;
}

static void set_play_error(int error)
{
    g_last_play_error = error;
}

static void clear_play_error(void)
{
    g_last_play_error = 0;
}

static void wait_playback_idle(void)
{
    g_stop_requested = true;
    g_pause_requested = false;
    while (g_playing)
        svcSleepThread(100000);
    g_paused = false;
    g_have_resume = false;
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

void audio_stop(void)
{
    g_stop_requested = true;
    g_pause_requested = false;
    g_paused = false;
    g_have_resume = false;
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
    g_resume_sample = sample;
    g_have_resume = true;
    g_paused = true;
    g_pause_requested = false;
}

int audio_end_is_pause(void)
{
    return g_pause_requested && !g_stop_requested;
}

static void playback_thread_func(void *arg)
{
    int result;

    (void)arg;
    g_playing = true;
    g_stop_requested = false;
    g_pause_requested = false;
    result = audio_play_file(g_play_path);
    if (result != 0 && !g_stop_requested)
        set_play_error(result);
    else if (result == 0 && !g_paused)
        clear_play_error();
    if (!g_paused)
        g_have_resume = false;
    g_playing = false;
}

static int start_playback_thread(int keep_resume)
{
    if (!keep_resume)
        g_have_resume = false;

    g_stop_requested = false;
    g_pause_requested = false;

    if (!threadCreate(playback_thread_func, NULL, 0x8000, 0x30, -1, true))
        return -7;
    return 0;
}

void audio_resume(void)
{
    if (!g_paused || g_playing)
        return;
    g_paused = false;
    (void)start_playback_thread(1);
}

int audio_play_file_async(const char *path)
{
    int r;

    if (!g_audio_initialized || !path) {
        set_play_error(-1);
        return -1;
    }

    if (g_playing)
        wait_playback_idle();

    clear_play_error();
    g_paused = false;
    strncpy(g_play_path, path, sizeof(g_play_path) - 1);
    g_play_path[sizeof(g_play_path) - 1] = '\0';

    r = start_playback_thread(0);
    if (r != 0)
        set_play_error(r);
    return r;
}

int audio_is_playing(void)
{
    return g_playing;
}

int audio_is_paused(void)
{
    return g_paused && !g_playing;
}

static ndspWaveBuf g_waveBufs[N_WAVEBUFS];
static int g_channel_used = -1;

static int is_flac_path(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot && (strcasecmp(dot, ".flac") == 0);
}

static int is_flac_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[4];
    int ok = (fread(buf, 1, 4, f) == 4 && memcmp(buf, FLAC_MAGIC, 4) == 0);
    fclose(f);
    return ok;
}

int audio_play_file(const char *path)
{
    if (!path || !g_audio_initialized)
        return -1;

    if (is_flac_path(path) || is_flac_file(path))
        return audio_play_flac(path);

    g_stop_requested = false;
    g_pause_requested = false;

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
        if (resume >= 0)
            libvgmstream_seek(vg, resume);
    }

    const libvgmstream_format_t *fmt = vg->format;
    int ch = fmt->channels;
    int sr = fmt->sample_rate;
    if (ch < 1 || ch > 2 || sr < 300 || sr > 48000) {
        libvgmstream_free(vg);
        return -4;
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
