/* Minimal audio playback using vgmstream + 3DS NDSP.
 * Supports all formats vgmstream supports (WAV, BRSTM, ADPCM, OGG, etc.) with no optional libs. */

#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include "audio.h"
#include "audio_ctrl.h"
#include "audio_ctrl_internal.h"
#include "audio_viz.h"
#include "error_log.h"
#include "file_magic.h"
#include "libvgmstream.h"
#include "pcm_ring.h"

#define SAMPLES_PER_BUF  (4096)
#define N_WAVEBUFS       (4)
/* Ring buffer: ~2 s stereo @ 44.1 kHz, same decode-ahead slack as FLAC.
 * WAV pulls ~176 KB/s straight off the SD card, and SD reads stall for
 * hundreds of ms at times; the old inline-decode path had only the wavebuf
 * queue (~370 ms) to ride that out, which is why WAV alone stuttered. */
#define RING_BYTES       (384 * 1024)
#define DECODE_STACK     0x8000
/* svcSleepThread takes nanoseconds; 8 ms per idle pass vs a ~93 ms buffer
 * period. (The old value 8000 was meant as µs but slept 8 µs — a busy poll.) */
#define PLAYBACK_YIELD_NS  (8000000LL)

/* Higher priority than the UI/main thread (0x30): keeping NDSP fed wins
 * over drawing; the loop sleeps every pass, so the UI is never starved. */
#define PLAYBACK_THREAD_PRIO 0x2C

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

    if (!threadCreate(playback_thread_func, NULL, 0x8000,
                      PLAYBACK_THREAD_PRIO, -1, true)) {
        error_log_set_site("async:playback_thread");
        return -7;
    }
    return 0;
}

void audio_resume(void)
{
    int r;

    if (!audio_is_paused() || audio_is_playing())
        return;
    audio_ctrl_set_paused_flag(0);
    r = start_playback_thread(1);
    if (r != 0)
        audio_set_play_error(r);
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
        error_log_set_site("async:bad_args");
        error_log_set_fail_path(path);
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
    error_log_set_fail_path(g_play_path);

    r = start_playback_thread(0);
    if (r != 0)
        audio_set_play_error(r);
    return r;
}

static ndspWaveBuf g_waveBufs[N_WAVEBUFS];
static int g_channel_used = -1;

/* Decode-ahead ring shared between the vgmstream decode thread (producer,
 * does the SD reads) and the playback loop (consumer, feeds NDSP). Same
 * scheme as the FLAC player. */
typedef struct {
    libvgmstream_t *vg;
    unsigned int    channels;
    uint8_t        *ring;
    size_t          ring_size;
    size_t          chunk_bytes;   /* bytes per decode chunk */
    volatile size_t write_pos;
    volatile size_t read_pos;
    volatile bool   decode_done;
    volatile bool   stop;
    LightLock       lock;
} vgm_ring_t;

static void vgm_decode_thread(void *arg)
{
    vgm_ring_t *r = (vgm_ring_t *)arg;
    int16_t    *decode_buf = (int16_t *)linearAlloc(r->chunk_bytes);

    if (!decode_buf) {
        r->decode_done = true;
        return;
    }

    while (!r->stop && !audio_playback_should_exit()) {
        if (pcm_ring_producer_full(r->write_pos, r->read_pos,
                                   r->ring_size, r->chunk_bytes)) {
            svcSleepThread(1000000); /* 1 ms: ring full, let consumer drain */
            continue;
        }

        int got = libvgmstream_fill(r->vg, decode_buf, SAMPLES_PER_BUF);
        if (got <= 0) {
            if (r->vg->decoder->done)
                break;
            svcSleepThread(1000000);
            continue;
        }

        size_t to_write = (size_t)got * r->channels * sizeof(int16_t);
        pcm_ring_write(r->ring, r->ring_size, r->write_pos,
                       decode_buf, to_write);
        LightLock_Lock(&r->lock);
        r->write_pos += to_write;
        LightLock_Unlock(&r->lock);
    }
    r->decode_done = true;
    linearFree(decode_buf);
}

int audio_play_file(const char *path)
{
    if (!path || !g_audio_initialized) {
        error_log_set_site("vgm:bad_args");
        return -1;
    }

    error_log_set_fail_path(path);

    switch (audio_route_for_path(path)) {
    case AUDIO_ROUTE_FLAC:
        return audio_play_flac(path);
    case AUDIO_ROUTE_MP3:
        return audio_play_mp3(path);
    case AUDIO_ROUTE_OPUS:
        return audio_play_opus(path);
    default:
        break;
    }

    audio_ctrl_clear_exit_flags();
    audio_viz_reset();

    libstreamfile_t *libsf = libstreamfile_open_from_stdio(path);
    if (!libsf) {
        error_log_set_site("vgm:open");
        return -2;
    }

    libvgmstream_config_t cfg = {0};
    cfg.allow_play_forever = false;
    libvgmstream_t *vg = libvgmstream_create(libsf, 0, &cfg);
    libstreamfile_close(libsf);
    if (!vg) {
        error_log_set_site("vgm:create");
        return -3;
    }

    int64_t resume   = audio_take_resume_sample();
    int     resuming = (resume >= 0);
    if (resuming) {
        libvgmstream_seek(vg, resume);
        audio_ctrl_set_position(resume);
    }

    const libvgmstream_format_t *fmt = vg->format;
    int ch = fmt->channels;
    int sr = fmt->sample_rate;
    if (ch < 1 || ch > 2 || sr < 300 || sr > 48000) {
        libvgmstream_free(vg);
        error_log_set_site("vgm:format");
        return -4;
    }

    {
        int64_t dur = fmt->play_samples;
        if (dur <= 0)
            dur = fmt->stream_samples;
        audio_ctrl_set_duration(dur);
        audio_ctrl_set_sample_rate(sr);
    }

    size_t chunk_bytes = (size_t)SAMPLES_PER_BUF * (size_t)ch * sizeof(int16_t);

    vgm_ring_t ring = {0};
    ring.vg          = vg;
    ring.channels    = (unsigned int)ch;
    ring.chunk_bytes = chunk_bytes;
    ring.ring_size   = RING_BYTES;
    ring.ring        = (uint8_t *)linearAlloc(ring.ring_size);
    if (!ring.ring) {
        libvgmstream_free(vg);
        error_log_set_site("vgm:ring_alloc");
        return -5;
    }
    LightLock_Init(&ring.lock);

    /* Decode (and all SD reads) off the playback loop; -1 = any core. */
    Thread th = threadCreate(vgm_decode_thread, &ring, DECODE_STACK,
                             0x30, -1, false);
    if (!th) {
        linearFree(ring.ring);
        libvgmstream_free(vg);
        error_log_set_site("vgm:decode_thread");
        return -6;
    }

    int ndsp_format = (ch == 2) ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16;
    int channel_id = 0;
    ndspChnReset(channel_id);
    ndspChnSetInterp(channel_id, NDSP_INTERP_LINEAR);
    ndspChnSetRate(channel_id, (float)sr);
    ndspChnSetFormat(channel_id, ndsp_format);
    ndspChnSetMix(channel_id, (float[12]){ 1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
    g_channel_used = channel_id;

    size_t buf_bytes = chunk_bytes;
    void *bufs[N_WAVEBUFS];
    for (int i = 0; i < N_WAVEBUFS; i++) {
        bufs[i] = linearAlloc(buf_bytes);
        if (!bufs[i]) {
            ring.stop = true;
            threadJoin(th, U64_MAX);
            threadFree(th);
            for (int j = 0; j < i; j++)
                linearFree(bufs[j]);
            linearFree(ring.ring);
            libvgmstream_free(vg);
            g_channel_used = -1;
            error_log_set_site("vgm:wavebuf_alloc");
            return -7;
        }
        memset(&g_waveBufs[i], 0, sizeof(ndspWaveBuf));
        g_waveBufs[i].data_vaddr = bufs[i];
        g_waveBufs[i].status     = NDSP_WBUF_FREE;
    }

    /* Pre-fill before first output; keep resume start short. */
    size_t prefill = pcm_ring_prefill_target(resuming, chunk_bytes,
                                             (unsigned)sr, (unsigned)ch,
                                             ring.ring_size);
    while (!audio_playback_should_exit()) {
        LightLock_Lock(&ring.lock);
        size_t avail = pcm_ring_avail(ring.write_pos, ring.read_pos);
        LightLock_Unlock(&ring.lock);
        if (avail >= prefill || ring.decode_done)
            break;
        svcSleepThread(100000); /* 100 µs */
    }

    int next = 0;
    bool stream_done = false;
    uint64_t samples_fed = resuming ? (uint64_t)resume : 0;

    while (!stream_done && !audio_playback_should_exit()) {
        ndspWaveBuf *wb = &g_waveBufs[next];
        if (wb->status == NDSP_WBUF_FREE || wb->status == NDSP_WBUF_DONE) {
            LightLock_Lock(&ring.lock);
            size_t avail = pcm_ring_avail(ring.write_pos, ring.read_pos);
            LightLock_Unlock(&ring.lock);
            if (avail >= chunk_bytes) {
                pcm_ring_read(ring.ring, ring.ring_size, ring.read_pos,
                              bufs[next], chunk_bytes);
                LightLock_Lock(&ring.lock);
                ring.read_pos += chunk_bytes;
                LightLock_Unlock(&ring.lock);
                wb->nsamples = SAMPLES_PER_BUF;
                audio_viz_feed((const int16_t *)bufs[next],
                               SAMPLES_PER_BUF, (unsigned int)ch, sr);
                DSP_FlushDataCache(bufs[next], buf_bytes);
                ndspChnWaveBufAdd(channel_id, wb);
                samples_fed += SAMPLES_PER_BUF;
                audio_ctrl_set_position((int64_t)samples_fed);
                next = (next + 1) % N_WAVEBUFS;
            } else if (ring.decode_done) {
                /* Partial last chunk or exact end. */
                if (avail > 0) {
                    size_t frame_bytes = (size_t)ch * sizeof(int16_t);
                    size_t samples;
                    size_t rp_snap;

                    LightLock_Lock(&ring.lock);
                    rp_snap = ring.read_pos;
                    samples = pcm_ring_pop_partial(
                        ring.ring, ring.ring_size, ring.write_pos, &rp_snap,
                        bufs[next], chunk_bytes, frame_bytes);
                    ring.read_pos = rp_snap;
                    LightLock_Unlock(&ring.lock);
                    wb->nsamples = (u32)samples;
                    audio_viz_feed((const int16_t *)bufs[next],
                                   samples, (unsigned int)ch, sr);
                    DSP_FlushDataCache(bufs[next], buf_bytes);
                    ndspChnWaveBufAdd(channel_id, wb);
                    samples_fed += samples;
                    audio_ctrl_set_position((int64_t)samples_fed);
                    next = (next + 1) % N_WAVEBUFS;
                }
                stream_done = true;
            } else {
                svcSleepThread(PLAYBACK_YIELD_NS);
            }
        } else {
            LightLock_Lock(&ring.lock);
            size_t a = pcm_ring_avail(ring.write_pos, ring.read_pos);
            LightLock_Unlock(&ring.lock);
            if (ring.decode_done && a == 0)
                stream_done = true;
            else
                svcSleepThread(PLAYBACK_YIELD_NS);
        }
    }

    /* Let the queued tail play out on a natural end (not stop/pause). */
    if (stream_done && !audio_playback_should_exit()) {
        bool draining = true;
        while (draining && !audio_playback_should_exit()) {
            draining = false;
            for (int i = 0; i < N_WAVEBUFS; i++)
                if (g_waveBufs[i].status == NDSP_WBUF_PLAYING
                    || g_waveBufs[i].status == NDSP_WBUF_QUEUED)
                    draining = true;
            if (draining)
                svcSleepThread(PLAYBACK_YIELD_NS);
        }
    }

    ring.stop = true;
    int pausing = audio_end_is_pause();
    threadJoin(th, U64_MAX);
    threadFree(th);
    ndspChnWaveBufClear(channel_id);

    if (pausing)
        audio_note_paused_at((int64_t)samples_fed);

    for (int i = 0; i < N_WAVEBUFS; i++)
        linearFree(bufs[i]);
    linearFree(ring.ring);
    libvgmstream_free(vg);
    g_channel_used = -1;
    return 0;
}
