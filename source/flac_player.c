/* FLAC playback via dr_flac with decode-ahead threading.
 * Decode runs on core 1 into a ring buffer; main thread feeds NDSP from the buffer. */

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_WCHAR
#include "dr_flac.h"

#include <3ds.h>
#include <string.h>
#include "audio.h"

#define SAMPLES_PER_BUF  4096
#define N_WAVEBUFS       4
/* Ring buffer: ~2 seconds stereo @ 44.1kHz = 176400 bytes/channel * 2 = 352800. Use 384 KB. */
#define RING_BYTES       (384 * 1024)
#define DECODE_STACK     0x8000
#define PLAYBACK_YIELD_US  (8000)

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
        size_t used = r->write_pos - r->read_pos;
        if (used > r->ring_size - r->chunk_bytes) {
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
        size_t w = r->write_pos % r->ring_size;
        if (w + to_write <= r->ring_size) {
            memcpy(r->ring + w, decode_buf, to_write);
        } else {
            size_t first = r->ring_size - w;
            memcpy(r->ring + w, decode_buf, first);
            memcpy(r->ring, (uint8_t *)decode_buf + first, to_write - first);
        }
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
    if (resuming)
        drflac_seek_to_pcm_frame(flac, (drflac_uint64)resume);

    unsigned int ch = flac->channels;
    unsigned int sr = flac->sampleRate;
    if (ch < 1 || ch > 2 || sr < 300 || sr > 48000) {
        drflac_close(flac);
        return -2;
    }

    size_t chunk_bytes = SAMPLES_PER_BUF * ch * sizeof(drflac_int16);

    flac_ring_t ring = {0};
    ring.flac = flac;
    ring.channels = ch;
    ring.chunk_bytes = chunk_bytes;
    ring.ring_size = RING_BYTES;
    ring.ring = (uint8_t *)linearAlloc(ring.ring_size);
    if (!ring.ring) {
        drflac_close(flac);
        return -3;
    }
    LightLock_Init(&ring.lock);

    /* Start decode thread; -1 = any core (core 1 when available e.g. CIA on N3DS) */
    Thread th = threadCreate(decode_thread, &ring, DECODE_STACK, 0x30, -1, false);
    if (!th) {
        linearFree(ring.ring);
        drflac_close(flac);
        return -4;
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
    size_t prefill = resuming ? (chunk_bytes * 2u) : (size_t)(sr * ch * 2);
    if (prefill > ring.ring_size / 2)
        prefill = ring.ring_size / 2;
    while (!audio_playback_should_exit()) {
        LightLock_Lock(&ring.lock);
        size_t avail = ring.write_pos - ring.read_pos;
        LightLock_Unlock(&ring.lock);
        if (avail >= prefill || ring.decode_done)
            break;
        svcSleepThread(100000); /* 100 µs */
    }

    int next = 0;
    bool stream_done = false;
    uint64_t samples_fed = 0;
    int pausing = 0;

    while (!stream_done && !audio_playback_should_exit()) {
        ndspWaveBuf *wb = &waveBufs[next];
        if (wb->status == NDSP_WBUF_FREE || wb->status == NDSP_WBUF_DONE) {
            LightLock_Lock(&ring.lock);
            size_t avail = ring.write_pos - ring.read_pos;
            LightLock_Unlock(&ring.lock);
            if (avail >= chunk_bytes) {
                size_t rp = ring.read_pos % ring.ring_size;
                if (rp + chunk_bytes <= ring.ring_size) {
                    memcpy(bufs[next], ring.ring + rp, chunk_bytes);
                } else {
                    size_t first = ring.ring_size - rp;
                    memcpy(bufs[next], ring.ring + rp, first);
                    memcpy((uint8_t *)bufs[next] + first, ring.ring, chunk_bytes - first);
                }
                LightLock_Lock(&ring.lock);
                ring.read_pos += chunk_bytes;
                LightLock_Unlock(&ring.lock);
                wb->nsamples = SAMPLES_PER_BUF;
                DSP_FlushDataCache(bufs[next], buf_bytes);
                ndspChnWaveBufAdd(channel_id, wb);
                samples_fed += SAMPLES_PER_BUF;
                next = (next + 1) % N_WAVEBUFS;
            } else if (ring.decode_done) {
                /* Partial last chunk or exact end */
                if (avail > 0) {
                    size_t samples = avail / (ch * sizeof(drflac_int16));
                    size_t to_copy = samples * ch * sizeof(drflac_int16);
                    size_t rp = ring.read_pos % ring.ring_size;
                    if (rp + to_copy <= ring.ring_size)
                        memcpy(bufs[next], ring.ring + rp, to_copy);
                    else {
                        size_t first = ring.ring_size - rp;
                        memcpy(bufs[next], ring.ring + rp, first);
                        memcpy((uint8_t *)bufs[next] + first, ring.ring, to_copy - first);
                    }
                    memset((uint8_t *)bufs[next] + to_copy, 0, chunk_bytes - to_copy);
                    LightLock_Lock(&ring.lock);
                    ring.read_pos += to_copy;
                    LightLock_Unlock(&ring.lock);
                    wb->nsamples = (u32)samples;
                    DSP_FlushDataCache(bufs[next], buf_bytes);
                    ndspChnWaveBufAdd(channel_id, wb);
                    samples_fed += samples;
                    next = (next + 1) % N_WAVEBUFS;
                }
                stream_done = true;
            } else {
                svcSleepThread(PLAYBACK_YIELD_US);
            }
        } else {
            LightLock_Lock(&ring.lock);
            size_t a = ring.write_pos - ring.read_pos;
            LightLock_Unlock(&ring.lock);
            if (ring.decode_done && a == 0)
                stream_done = true;
            else
                svcSleepThread(PLAYBACK_YIELD_US);
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
