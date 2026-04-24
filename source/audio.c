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
#define N_WAVEBUFS       (2)

static bool g_audio_initialized = false;
static volatile bool g_stop_requested = false;
static volatile bool g_playing = false;
static Thread g_play_thread = NULL;
static char g_play_path[256];

static void wait_playback_idle(void)
{
    if (g_play_thread) {
        g_stop_requested = true;
        threadJoin(g_play_thread, U64_MAX);
        g_play_thread = NULL;
    }
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
}

int audio_should_stop(void)
{
    return g_stop_requested;
}

static void playback_thread_func(void *arg)
{
    (void)arg;
    g_playing = true;
    g_stop_requested = false;
    audio_play_file(g_play_path);
    g_playing = false;
}

int audio_play_file_async(const char *path)
{
    if (!path || !g_audio_initialized)
        return -1;
    if (g_playing)
        return -6; /* already playing */
    strncpy(g_play_path, path, sizeof(g_play_path) - 1);
    g_play_path[sizeof(g_play_path) - 1] = '\0';

    g_play_thread = threadCreate(playback_thread_func, NULL, 0x8000, 0x30, -1, false);
    if (!g_play_thread)
        return -7;
    return 0;
}

int audio_is_playing(void)
{
    return g_playing;
}

static ndspWaveBuf g_waveBufs[N_WAVEBUFS];
static int g_channel_used = -1;

static int is_flac_path(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot && (strcasecmp(dot, ".flac") == 0);
}

/* Peek at file magic bytes; returns true if file starts with FLAC signature. */
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

    /* FLAC by extension or by file content (e.g. music.wav containing FLAC) */
    if (is_flac_path(path) || is_flac_file(path))
        return audio_play_flac(path);

    g_stop_requested = false;

    libstreamfile_t *libsf = libstreamfile_open_from_stdio(path);
    if (!libsf) {
        return -2; /* open failed */
    }

    libvgmstream_config_t cfg = {0};
    cfg.allow_play_forever = false;
    libvgmstream_t *vg = libvgmstream_create(libsf, 0, &cfg);
    libstreamfile_close(libsf);
    if (!vg) {
        return -3; /* format not supported or init failed */
    }

    const libvgmstream_format_t *fmt = vg->format;
    int ch = fmt->channels;
    int sr = fmt->sample_rate;
    if (ch < 1 || ch > 2 || sr < 300 || sr > 48000) {
        libvgmstream_free(vg);
        return -4; /* unsupported channel/count or rate for NDSP */
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

    while (!g_stop_requested) {
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
        gspWaitForVBlank();
    }

    ndspChnWaveBufClear(channel_id);
    for (int i = 0; i < N_WAVEBUFS; i++)
        linearFree(bufs[i]);
    libvgmstream_free(vg);
    g_channel_used = -1;
    return ret;
}
