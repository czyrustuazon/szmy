/* Opus playback via libopusfile with decode-ahead threading.
 * Decode runs into a ring buffer; main thread feeds NDSP from the buffer. */

#include <3ds.h>
#include <string.h>
#include "audio.h"
#include "audio_ctrl.h"
#include "audio_viz.h"
#include "error_log.h"
#include "pcm_ring.h"

#if defined(UNIT_TEST)
#include "host_opusfile.h"
#elif defined(ENABLE_OPUS)
#include <opusfile.h>
#endif

#if defined(UNIT_TEST) || defined(ENABLE_OPUS)

#define SAMPLES_PER_BUF  4096
#define N_WAVEBUFS       4
#define RING_BYTES       (384 * 1024)
#define DECODE_STACK     0x8000
#define PLAYBACK_YIELD_NS  (8000000LL)
/* Opus decode output is always 48 kHz. */
#define OPUS_RATE        48000

#ifdef UNIT_TEST
static volatile int g_fake_producer_full;
static volatile int g_force_decode_spin;
static volatile int g_decode_in_loop;
static volatile int g_spin_stop_after;
static volatile int g_spin_iters;

void opus_test_set_fake_producer_full(int times)
{
    g_fake_producer_full = times;
}

void opus_test_set_decode_spin(int spin)
{
    g_force_decode_spin = spin;
    if (!spin) {
        g_decode_in_loop  = 0;
        g_spin_stop_after = 0;
        g_spin_iters      = 0;
    }
}

void opus_test_set_decode_spin_stop_after(int iters)
{
    g_spin_stop_after = iters;
    g_spin_iters      = 0;
}
#endif

typedef struct {
    OggOpusFile *of;
    unsigned int channels;
    uint8_t *ring;
    size_t ring_size;
    size_t chunk_bytes;
    volatile size_t write_pos;
    volatile size_t read_pos;
    volatile bool decode_done;
    volatile bool stop;
    LightLock lock;
} opus_ring_t;

/* Fill up to max_frames PCM frames into buf. Returns frames read, or 0 at EOF. */
static int opus_read_frames(OggOpusFile *of, int16_t *buf, int channels, int max_frames)
{
    int got = 0;

    while (got < max_frames) {
        int n = op_read(of, buf + got * channels, (max_frames - got) * channels, NULL);
        if (n < 0) {
            if (n == OP_HOLE)
                continue;
            break;
        }
        if (n == 0)
            break;
        got += n;
    }
    return got;
}

static void decode_thread(void *arg)
{
    opus_ring_t *r = (opus_ring_t *)arg;
    int16_t *decode_buf = (int16_t *)linearAlloc(r->chunk_bytes);
    if (!decode_buf) {
        r->decode_done = true;
        return;
    }

    while (!r->stop && !audio_playback_should_exit()) {
#ifdef UNIT_TEST
        if (g_force_decode_spin) {
            g_decode_in_loop = 1;
            if (g_spin_stop_after > 0 && ++g_spin_iters >= g_spin_stop_after) {
                audio_stop();
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
            svcSleepThread(1000000);
            continue;
        }

        int got = opus_read_frames(r->of, decode_buf, (int)r->channels, SAMPLES_PER_BUF);
        if (got <= 0) {
            r->decode_done = true;
            linearFree(decode_buf);
            return;
        }

        size_t to_write = (size_t)got * r->channels * sizeof(int16_t);
        pcm_ring_write(r->ring, r->ring_size, r->write_pos, decode_buf, to_write);
        LightLock_Lock(&r->lock);
        r->write_pos += to_write;
        LightLock_Unlock(&r->lock);
    }
    linearFree(decode_buf);
}

int audio_play_opus(const char *path)
{
    int err = 0;
    OggOpusFile *of;

    if (!path)
        return -1;

    of = op_open_file(path, &err);
    if (!of)
        return -1;

    int64_t resume = audio_take_resume_sample();
    int resuming = (resume >= 0);
    if (resuming) {
        if (op_pcm_seek(of, resume) != 0) {
            op_free(of);
            return -2;
        }
        audio_ctrl_set_position(resume);
    }

    int ch = op_channel_count(of, -1);
    unsigned int sr = OPUS_RATE;
    if (ch < 1 || ch > 2) {
        op_free(of);
        return -4;
    }

    ogg_int64_t total = op_pcm_total(of, -1);
    if (total < 0)
        total = 0;
    audio_ctrl_set_duration((int64_t)total);
    audio_ctrl_set_sample_rate((int32_t)sr);
    audio_viz_reset();

    size_t chunk_bytes = SAMPLES_PER_BUF * (unsigned)ch * sizeof(int16_t);

    opus_ring_t ring = {0};
    ring.of = of;
    ring.channels = (unsigned)ch;
    ring.chunk_bytes = chunk_bytes;
    ring.ring_size = RING_BYTES;
    ring.ring = (uint8_t *)linearAlloc(ring.ring_size);
    if (!ring.ring) {
        op_free(of);
        return -5;
    }
    LightLock_Init(&ring.lock);

    Thread th = threadCreate(decode_thread, &ring, DECODE_STACK, 0x30, -1, false);
    if (!th) {
        linearFree(ring.ring);
        op_free(of);
        error_log_set_site("opus:decode_thread");
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
            op_free(of);
            return -5;
        }
        memset(&waveBufs[i], 0, sizeof(ndspWaveBuf));
        waveBufs[i].data_vaddr = bufs[i];
        waveBufs[i].status = NDSP_WBUF_FREE;
    }

    size_t prefill = pcm_ring_prefill_target(resuming, chunk_bytes, sr, (unsigned)ch, ring.ring_size);
    while (!audio_playback_should_exit()) {
        LightLock_Lock(&ring.lock);
        size_t avail = pcm_ring_avail(ring.write_pos, ring.read_pos);
        LightLock_Unlock(&ring.lock);
        if (avail >= prefill || ring.decode_done)
            break;
        svcSleepThread(100000);
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
                               SAMPLES_PER_BUF, (unsigned)ch, (int)sr);
                DSP_FlushDataCache(bufs[next], buf_bytes);
                ndspChnWaveBufAdd(channel_id, wb);
                samples_fed += SAMPLES_PER_BUF;
                audio_ctrl_set_position((int64_t)samples_fed);
                next = (next + 1) % N_WAVEBUFS;
            } else if (ring.decode_done) {
                if (avail > 0) {
                    size_t frame_bytes = (unsigned)ch * sizeof(int16_t);
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
                                   samples, (unsigned)ch, (int)sr);
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
    op_free(of);
    return 0;
}

#else /* !UNIT_TEST && !ENABLE_OPUS */

int audio_play_opus(const char *path)
{
    (void)path;
    return -3;
}

#endif
