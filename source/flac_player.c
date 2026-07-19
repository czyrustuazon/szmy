/* FLAC playback via dr_flac with decode-ahead threading.
 * Decode runs on core 1 into a ring buffer; main thread feeds NDSP from the buffer. */

#ifndef UNIT_TEST
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_WCHAR
#include "dr_flac.h"
#else
#include "host_drflac.h"
#endif

#include <3ds.h>
#include <string.h>
#include "audio.h"
#include "audio_ctrl.h"
#include "audio_viz.h"
#include "pcm_ring.h"

#define SAMPLES_PER_BUF  4096
#define N_WAVEBUFS       4
/* Ring buffer: ~2 seconds stereo @ 44.1kHz = 176400 bytes/channel * 2 = 352800. Use 384 KB. */
#define RING_BYTES       (384 * 1024)
#define DECODE_STACK     0x8000
/* svcSleepThread takes nanoseconds; 8 ms per idle pass vs a ~93 ms buffer
 * period. (The old value 8000 was meant as µs but slept 8 µs — a busy poll.) */
#define PLAYBACK_YIELD_NS  (8000000LL)

#ifdef UNIT_TEST
/* Force decode_thread through the producer_full yield path N times. */
static volatile int g_fake_producer_full;
/* Keep decode in the while-loop until ring.stop / should_exit (for stop-branch coverage). */
static volatile int g_force_decode_spin;
static volatile int g_decode_in_loop;
/* After N spin iterations, call audio_stop() while ring.stop is still false. */
static volatile int g_spin_stop_after;
static volatile int g_spin_iters;

void flac_test_set_fake_producer_full(int times)
{
    g_fake_producer_full = times;
}

void flac_test_set_decode_spin(int spin)
{
    g_force_decode_spin = spin;
    if (!spin) {
        g_decode_in_loop   = 0;
        g_spin_stop_after  = 0;
        g_spin_iters       = 0;
    }
}

void flac_test_set_decode_spin_stop_after(int iters)
{
    g_spin_stop_after = iters;
    g_spin_iters      = 0;
}
#endif

typedef struct {
    drflac *flac;
    unsigned int channels;
    uint8_t *ring;
    size_t ring_size;
    size_t chunk_bytes;   /* bytes per decode chunk (4096 samples * ch * 2) */
    volatile size_t write_pos;
    volatile size_t read_pos;
    volatile bool decode_done;
    volatile bool stop;
    LightLock lock;
} flac_ring_t;

static void decode_thread(void *arg)
{
    flac_ring_t *r = (flac_ring_t *)arg;
    drflac_int16 *decode_buf = (drflac_int16 *)linearAlloc(r->chunk_bytes);
    if (!decode_buf) {
        r->decode_done = true;
        return;
    }

    while (!r->stop && !audio_playback_should_exit()) {
#ifdef UNIT_TEST
        if (g_force_decode_spin) {
            g_decode_in_loop = 1;
            if (g_spin_stop_after > 0 && ++g_spin_iters >= g_spin_stop_after) {
                audio_stop(); /* should_exit while r->stop still false */
                /* Recheck while immediately — don't sleep or main may set
                 * ring.stop first and short-circuit past should_exit. */
                continue;
            }
            svcSleepThread(100000);
            continue;
        }
#endif
        int full = pcm_ring_producer_full(r->write_pos, r->read_pos, r->ring_size, r->chunk_bytes);
#ifdef UNIT_TEST
        if (g_fake_producer_full > 0) {
            g_fake_producer_full--;
            full = 1;
        }
#endif
        if (full) {
            /* Ring full, yield to let consumer drain */
            svcSleepThread(1000000); /* 1 ms */
            continue;
        }

        drflac_uint64 got = drflac_read_pcm_frames_s16(r->flac, SAMPLES_PER_BUF, decode_buf);
        if (got == 0) {
            r->decode_done = true;
            linearFree(decode_buf);
            return;
        }

        size_t to_write = (size_t)got * r->channels * sizeof(drflac_int16);
        pcm_ring_write(r->ring, r->ring_size, r->write_pos, decode_buf, to_write);
        LightLock_Lock(&r->lock);
        r->write_pos += to_write;
        LightLock_Unlock(&r->lock);
    }
    linearFree(decode_buf);
}

int audio_play_flac(const char *path)
{
    drflac *flac = drflac_open_file(path, NULL);
    if (!flac)
        return -1;

    int64_t resume = audio_take_resume_sample();
    int resuming = (resume >= 0);
    if (resuming) {
        drflac_seek_to_pcm_frame(flac, (drflac_uint64)resume);
        audio_ctrl_set_position(resume);
    }

    /* Error codes follow audio_error_message(): -4 bad channels/rate,
     * -5 out of memory, -7 could not start playback. */
    unsigned int ch = flac->channels;
    unsigned int sr = flac->sampleRate;
    if (ch < 1 || ch > 2 || sr < 300 || sr > 48000) {
        drflac_close(flac);
        return -4;
    }

    audio_ctrl_set_duration((int64_t)flac->totalPCMFrameCount);
    audio_ctrl_set_sample_rate((int32_t)sr);
    audio_viz_reset();

    size_t chunk_bytes = SAMPLES_PER_BUF * ch * sizeof(drflac_int16);

    flac_ring_t ring = {0};
    ring.flac = flac;
    ring.channels = ch;
    ring.chunk_bytes = chunk_bytes;
    ring.ring_size = RING_BYTES;
    ring.ring = (uint8_t *)linearAlloc(ring.ring_size);
    if (!ring.ring) {
        drflac_close(flac);
        return -5;
    }
    LightLock_Init(&ring.lock);

    /* Start decode thread; -1 = any core (core 1 when available e.g. CIA on N3DS) */
    Thread th = threadCreate(decode_thread, &ring, DECODE_STACK, 0x30, -1, false);
    if (!th) {
        linearFree(ring.ring);
        drflac_close(flac);
        return -7;
    }

    int ndsp_format = (ch == 2) ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16;
    int channel_id = 0;

    ndspChnReset(channel_id);
    ndspChnSetInterp(channel_id, NDSP_INTERP_LINEAR);
    ndspChnSetRate(channel_id, (float)sr);
    ndspChnSetFormat(channel_id, ndsp_format);
    ndspChnSetMix(channel_id, (float[12]){ 1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });

    ndspWaveBuf waveBufs[N_WAVEBUFS];
    void *bufs[N_WAVEBUFS];
    size_t buf_bytes = chunk_bytes;

#ifdef UNIT_TEST
    /* Let a spinning decode enter the while before we allocate / fail wavebufs. */
    if (g_force_decode_spin) {
        while (!g_decode_in_loop && !ring.decode_done)
            svcSleepThread(100000);
    }
#endif

    for (int i = 0; i < N_WAVEBUFS; i++) {
        bufs[i] = linearAlloc(buf_bytes);
        if (!bufs[i]) {
            ring.stop = true;
            threadJoin(th, U64_MAX);
            for (int j = 0; j < i; j++)
                linearFree(bufs[j]);
            linearFree(ring.ring);
            drflac_close(flac);
            return -5;
        }
        memset(&waveBufs[i], 0, sizeof(ndspWaveBuf));
        waveBufs[i].data_vaddr = bufs[i];
        waveBufs[i].status = NDSP_WBUF_FREE;
    }

    /* Pre-fill before first output; keep resume start short. */
    size_t prefill = pcm_ring_prefill_target(resuming, chunk_bytes, sr, ch, ring.ring_size);
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
    int pausing = 0;

    while (!stream_done && !audio_playback_should_exit()) {
        ndspWaveBuf *wb = &waveBufs[next];
        if (wb->status == NDSP_WBUF_FREE || wb->status == NDSP_WBUF_DONE) {
            LightLock_Lock(&ring.lock);
            size_t avail = pcm_ring_avail(ring.write_pos, ring.read_pos);
            LightLock_Unlock(&ring.lock);
            if (avail >= chunk_bytes) {
                pcm_ring_read(ring.ring, ring.ring_size, ring.read_pos, bufs[next], chunk_bytes);
                LightLock_Lock(&ring.lock);
                ring.read_pos += chunk_bytes;
                LightLock_Unlock(&ring.lock);
                wb->nsamples = SAMPLES_PER_BUF;
                audio_viz_feed((const int16_t *)bufs[next],
                               SAMPLES_PER_BUF, ch, (int)sr);
                DSP_FlushDataCache(bufs[next], buf_bytes);
                ndspChnWaveBufAdd(channel_id, wb);
                samples_fed += SAMPLES_PER_BUF;
                audio_ctrl_set_position((int64_t)samples_fed);
                next = (next + 1) % N_WAVEBUFS;
            } else if (ring.decode_done) {
                /* Partial last chunk or exact end */
                if (avail > 0) {
                    size_t frame_bytes = ch * sizeof(drflac_int16);
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
                                   samples, ch, (int)sr);
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

    ring.stop = true;
    pausing = audio_end_is_pause();
    threadJoin(th, U64_MAX);
    ndspChnWaveBufClear(channel_id);

    if (pausing)
        audio_note_paused_at((int64_t)samples_fed);

    for (int i = 0; i < N_WAVEBUFS; i++)
        linearFree(bufs[i]);
    linearFree(ring.ring);
    drflac_close(flac);
    return 0;
}
