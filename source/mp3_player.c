/* MP3 playback via minimp3 with decode-ahead threading (no libmpg123 required).
 * Must match vgmstream's minimp3 build (float decode → s16 for NDSP). */

#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_NO_SIMD
#include "minimp3.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "audio.h"

#define SAMPLES_PER_BUF  4096
#define N_WAVEBUFS       4
#define RING_BYTES       (384 * 1024)
#define DECODE_STACK     0x8000
#define PLAYBACK_YIELD_US  (8000)
#define PCM16_MAX_SAMPLES  (MINIMP3_MAX_SAMPLES_PER_FRAME)

typedef struct {
    uint8_t *data;
    size_t data_size;
    size_t data_start;
    size_t read_pos;
    unsigned int channels;
    unsigned int sample_rate;
    mp3dec_t dec;
    uint8_t *ring;
    size_t ring_size;
    size_t chunk_bytes;
    volatile size_t write_pos;
    volatile size_t ring_read_pos;
    volatile bool decode_done;
    volatile bool stop;
    LightLock lock;
} mp3_ring_t;

static size_t skip_id3v2(const uint8_t *data, size_t size)
{
    if (size < 10 || memcmp(data, "ID3", 3) != 0)
        return 0;

    size_t tag_size = ((size_t)(data[6] & 0x7f) << 21) |
                      ((size_t)(data[7] & 0x7f) << 14) |
                      ((size_t)(data[8] & 0x7f) << 7) |
                      (size_t)(data[9] & 0x7f);
    return 10 + tag_size;
}

static uint8_t *load_mp3_file(const char *path, size_t *out_size, size_t *out_start)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    size_t n;

    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    n = (size_t)ftell(f);
    if (n == 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    buf = (uint8_t *)linearAlloc(n);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, n, f) != n) {
        linearFree(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);

    *out_size = n;
    *out_start = skip_id3v2(buf, n);
    return buf;
}

static int probe_mp3(const uint8_t *data, size_t size, size_t start,
                     unsigned int *channels, unsigned int *sample_rate)
{
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    size_t pos = start;

    mp3dec_init(&dec);
    while (pos + 4 < size) {
        int samples = mp3dec_decode_frame(&dec, data + pos, (int)(size - pos), NULL, &info);
        if (info.frame_bytes <= 0)
            break;
        pos += (size_t)info.frame_bytes;
        if (samples > 0 && info.hz > 0 && info.channels > 0) {
            *channels = (unsigned int)info.channels;
            *sample_rate = (unsigned int)info.hz;
            return 0;
        }
    }
    return -1;
}

static void decode_thread(void *arg)
{
    mp3_ring_t *r = (mp3_ring_t *)arg;
    mp3d_sample_t *decode_buf = (mp3d_sample_t *)linearAlloc(PCM16_MAX_SAMPLES * sizeof(mp3d_sample_t));
    int16_t *pcm16 = (int16_t *)linearAlloc(PCM16_MAX_SAMPLES * sizeof(int16_t));
    size_t mp3_pos = r->read_pos;
    mp3dec_frame_info_t info;

    if (!decode_buf || !pcm16) {
        linearFree(decode_buf);
        linearFree(pcm16);
        r->decode_done = true;
        return;
    }

    mp3dec_init(&r->dec);

    while (!r->stop && !audio_playback_should_exit()) {
        size_t used = r->write_pos - r->ring_read_pos;
        if (used > r->ring_size - r->chunk_bytes) {
            svcSleepThread(1000000);
            continue;
        }

        if (mp3_pos >= r->data_size) {
            r->decode_done = true;
            break;
        }

        int samples = mp3dec_decode_frame(&r->dec, r->data + mp3_pos,
                                            (int)(r->data_size - mp3_pos),
                                            decode_buf, &info);
        if (info.frame_bytes <= 0)
            break;
        mp3_pos += (size_t)info.frame_bytes;
        r->read_pos = mp3_pos;

        if (samples <= 0)
            continue;

        {
            int pcm_count = samples * info.channels;
            size_t to_write = (size_t)pcm_count * sizeof(int16_t);

            mp3dec_f32_to_s16(decode_buf, pcm16, pcm_count);
            size_t w = r->write_pos % r->ring_size;
            if (w + to_write <= r->ring_size) {
                memcpy(r->ring + w, pcm16, to_write);
            } else {
                size_t first = r->ring_size - w;
                memcpy(r->ring + w, pcm16, first);
                memcpy(r->ring, (uint8_t *)pcm16 + first, to_write - first);
            }
            LightLock_Lock(&r->lock);
            r->write_pos += to_write;
            LightLock_Unlock(&r->lock);
        }
    }

    linearFree(decode_buf);
    linearFree(pcm16);
    r->decode_done = true;
}

int audio_play_mp3(const char *path)
{
    size_t file_size = 0;
    size_t data_start = 0;
    uint8_t *file_data = load_mp3_file(path, &file_size, &data_start);
    unsigned int ch = 0;
    unsigned int sr = 0;
    size_t chunk_bytes;
    mp3_ring_t ring = {0};
    Thread th;
    int ndsp_format;
    int channel_id = 0;
    ndspWaveBuf waveBufs[N_WAVEBUFS];
    void *bufs[N_WAVEBUFS];
    size_t buf_bytes;
    size_t prefill;
    int next = 0;
    bool stream_done = false;
    uint64_t samples_fed = 0;
    int pausing = 0;
    int resuming;
    int64_t resume_samples;
    int i;

    if (!file_data)
        return -1;

    if (probe_mp3(file_data, file_size, data_start, &ch, &sr) != 0) {
        linearFree(file_data);
        return -2;
    }

    resume_samples = audio_take_resume_sample();
    resuming = (resume_samples >= 0);

    if (ch < 1 || ch > 2 || sr < 300 || sr > 48000) {
        linearFree(file_data);
        return -4;
    }

    chunk_bytes = SAMPLES_PER_BUF * ch * sizeof(int16_t);
    ring.data = file_data;
    ring.data_size = file_size;
    ring.data_start = data_start;
    ring.read_pos = data_start;
    ring.channels = ch;
    ring.sample_rate = sr;
    ring.chunk_bytes = chunk_bytes;
    ring.ring_size = RING_BYTES;
    ring.ring = (uint8_t *)linearAlloc(ring.ring_size);
    if (!ring.ring) {
        linearFree(file_data);
        return -5;
    }
    LightLock_Init(&ring.lock);

    if (resuming) {
        mp3dec_t skip_dec;
        mp3dec_frame_info_t info;
        size_t pos = data_start;
        uint64_t decoded = 0;

        mp3dec_init(&skip_dec);
        while (pos < file_size && decoded < (uint64_t)resume_samples) {
            int got = mp3dec_decode_frame(&skip_dec, file_data + pos,
                                          (int)(file_size - pos), NULL, &info);
            if (info.frame_bytes <= 0)
                break;
            pos += (size_t)info.frame_bytes;
            if (got > 0)
                decoded += (uint64_t)got;
        }
        ring.read_pos = pos;
    }

    th = threadCreate(decode_thread, &ring, DECODE_STACK, 0x30, -1, false);
    if (!th) {
        linearFree(ring.ring);
        linearFree(file_data);
        return -7;
    }

    ndsp_format = (ch == 2) ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16;
    ndspChnReset(channel_id);
    ndspChnSetInterp(channel_id, NDSP_INTERP_LINEAR);
    ndspChnSetRate(channel_id, (float)sr);
    ndspChnSetFormat(channel_id, ndsp_format);
    ndspChnSetMix(channel_id, (float[12]){ 1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });

    buf_bytes = chunk_bytes;
    for (i = 0; i < N_WAVEBUFS; i++) {
        bufs[i] = linearAlloc(buf_bytes);
        if (!bufs[i]) {
            ring.stop = true;
            threadJoin(th, U64_MAX);
            for (int j = 0; j < i; j++)
                linearFree(bufs[j]);
            linearFree(ring.ring);
            linearFree(file_data);
            return -5;
        }
        memset(&waveBufs[i], 0, sizeof(ndspWaveBuf));
        waveBufs[i].data_vaddr = bufs[i];
        waveBufs[i].status = NDSP_WBUF_FREE;
    }

    prefill = resuming ? (chunk_bytes * 2u) : (size_t)(sr * ch * 2);
    if (prefill > ring.ring_size / 2)
        prefill = ring.ring_size / 2;
    while (!audio_playback_should_exit()) {
        size_t avail;
        LightLock_Lock(&ring.lock);
        avail = ring.write_pos - ring.ring_read_pos;
        LightLock_Unlock(&ring.lock);
        if (avail >= prefill || ring.decode_done)
            break;
        svcSleepThread(100000);
    }

    while (!stream_done && !audio_playback_should_exit()) {
        ndspWaveBuf *wb = &waveBufs[next];
        if (wb->status == NDSP_WBUF_FREE || wb->status == NDSP_WBUF_DONE) {
            size_t avail;
            LightLock_Lock(&ring.lock);
            avail = ring.write_pos - ring.ring_read_pos;
            LightLock_Unlock(&ring.lock);
            if (avail >= chunk_bytes) {
                size_t rp = ring.ring_read_pos % ring.ring_size;
                if (rp + chunk_bytes <= ring.ring_size) {
                    memcpy(bufs[next], ring.ring + rp, chunk_bytes);
                } else {
                    size_t first = ring.ring_size - rp;
                    memcpy(bufs[next], ring.ring + rp, first);
                    memcpy((uint8_t *)bufs[next] + first, ring.ring, chunk_bytes - first);
                }
                LightLock_Lock(&ring.lock);
                ring.ring_read_pos += chunk_bytes;
                LightLock_Unlock(&ring.lock);
                wb->nsamples = SAMPLES_PER_BUF;
                DSP_FlushDataCache(bufs[next], buf_bytes);
                ndspChnWaveBufAdd(channel_id, wb);
                samples_fed += SAMPLES_PER_BUF;
                next = (next + 1) % N_WAVEBUFS;
            } else if (ring.decode_done) {
                if (avail > 0) {
                    size_t samples = avail / (ch * sizeof(int16_t));
                    size_t to_copy = samples * ch * sizeof(int16_t);
                    size_t rp = ring.ring_read_pos % ring.ring_size;
                    if (rp + to_copy <= ring.ring_size)
                        memcpy(bufs[next], ring.ring + rp, to_copy);
                    else {
                        size_t first = ring.ring_size - rp;
                        memcpy(bufs[next], ring.ring + rp, first);
                        memcpy((uint8_t *)bufs[next] + first, ring.ring, to_copy - first);
                    }
                    memset((uint8_t *)bufs[next] + to_copy, 0, chunk_bytes - to_copy);
                    LightLock_Lock(&ring.lock);
                    ring.ring_read_pos += to_copy;
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
            size_t a;
            LightLock_Lock(&ring.lock);
            a = ring.write_pos - ring.ring_read_pos;
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

    for (i = 0; i < N_WAVEBUFS; i++)
        linearFree(bufs[i]);
    linearFree(ring.ring);
    linearFree(file_data);
    return 0;
}
