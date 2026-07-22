/* MP3 playback via minimp3 with decode-ahead threading (no libmpg123 required).
 * Must match vgmstream's minimp3 build (float decode → s16 for NDSP). */

#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_NO_SIMD
#ifdef UNIT_TEST
#define MINIMP3_IMPLEMENTATION
#endif
#include "minimp3.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "audio.h"
#include "audio_ctrl.h"
#include "audio_viz.h"
#include "error_log.h"
#include "id3_util.h"
#include "pcm_ring.h"

#define SAMPLES_PER_BUF  4096
#define N_WAVEBUFS       4
#define RING_BYTES       (384 * 1024)
#define DECODE_STACK     0x8000
/* svcSleepThread takes nanoseconds; 8 ms per idle pass vs a ~93 ms buffer
 * period. (The old value 8000 was meant as µs but slept 8 µs — a busy poll.) */
#define PLAYBACK_YIELD_NS  (8000000LL)
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

#ifdef UNIT_TEST
static volatile int g_force_fseek_fail;
static volatile int g_force_fread_fail;

#define MP3_PROBE_MAX_STEPS 8
static struct {
    int samples;
    int frame_bytes;
    int hz;
    int channels;
} g_probe_steps[MP3_PROBE_MAX_STEPS];
static int g_probe_nsteps;
static int g_probe_i;

void mp3_test_set_fseek_fail(int fail)
{
    g_force_fseek_fail = fail;
}

void mp3_test_set_fread_fail(int fail)
{
    g_force_fread_fail = fail;
}

void mp3_test_clear_probe_steps(void)
{
    g_probe_nsteps = 0;
    g_probe_i      = 0;
}

void mp3_test_add_probe_step(int samples, int frame_bytes, int hz, int channels)
{
    if (g_probe_nsteps >= MP3_PROBE_MAX_STEPS)
        return;
    g_probe_steps[g_probe_nsteps].samples     = samples;
    g_probe_steps[g_probe_nsteps].frame_bytes = frame_bytes;
    g_probe_steps[g_probe_nsteps].hz          = hz;
    g_probe_steps[g_probe_nsteps].channels    = channels;
    g_probe_nsteps++;
}

static volatile int g_fail_decode_buf;
static volatile int g_fail_pcm16;

void mp3_test_set_decode_alloc_fail(int fail_decode_buf, int fail_pcm16)
{
    g_fail_decode_buf = fail_decode_buf;
    g_fail_pcm16      = fail_pcm16;
}

static volatile int g_fake_producer_full;
static volatile int g_force_decode_spin;
static volatile int g_decode_in_loop;
static volatile int g_spin_stop_after;
static volatile int g_spin_iters;

void mp3_test_set_fake_producer_full(int times)
{
    g_fake_producer_full = times;
}

void mp3_test_set_decode_spin(int spin)
{
    g_force_decode_spin = spin;
    if (!spin) {
        g_decode_in_loop  = 0;
        g_spin_stop_after = 0;
        g_spin_iters      = 0;
    }
}

void mp3_test_set_decode_spin_stop_after(int iters)
{
    g_spin_stop_after = iters;
    g_spin_iters      = 0;
}

static volatile int g_force_decode_frame_bytes_zero;

void mp3_test_set_decode_frame_bytes_zero(int fail)
{
    g_force_decode_frame_bytes_zero = fail;
}

static volatile int g_format_override;
static unsigned int g_override_ch;
static unsigned int g_override_sr;

void mp3_test_set_format_override(int enable, unsigned int ch, unsigned int sr)
{
    g_format_override = enable;
    g_override_ch     = ch;
    g_override_sr     = sr;
}

static volatile int g_force_skip_frame_bytes_zero;

void mp3_test_set_skip_frame_bytes_zero(int fail)
{
    g_force_skip_frame_bytes_zero = fail;
}

static volatile int g_decode_throttle_ns;
static volatile int g_decode_throttle_times;
static volatile int g_decode_max_chunks;

void mp3_test_set_decode_throttle(int times, int ns)
{
    g_decode_throttle_times = times;
    g_decode_throttle_ns    = ns;
}

void mp3_test_set_decode_max_chunks(int n)
{
    g_decode_max_chunks = n;
}
#endif

static uint8_t *load_mp3_file(const char *path, size_t *out_size, size_t *out_start)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    size_t n;
    int seek_rc;
    size_t got;

    if (!f)
        return NULL;

#ifdef UNIT_TEST
    seek_rc = g_force_fseek_fail ? -1 : fseek(f, 0, SEEK_END);
#else
    seek_rc = fseek(f, 0, SEEK_END);
#endif
    if (seek_rc != 0) {
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
#ifdef UNIT_TEST
    got = g_force_fread_fail ? 0 : fread(buf, 1, n, f);
#else
    got = fread(buf, 1, n, f);
#endif
    if (got != n) {
        linearFree(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);

    *out_size = n;
    *out_start = id3_skip_tag(buf, n);
    return buf;
}

/* Walks every frame header (no PCM decode with NULL output) so the summed
 * sample count is exact for VBR files and ignores trailing tags/junk —
 * a bitrate-based byte estimate is wrong for both. */
static int probe_mp3(const uint8_t *data, size_t size, size_t start,
                     unsigned int *channels, unsigned int *sample_rate,
                     int64_t *total_samples)
{
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    size_t pos = start;
    int found = 0;
    int64_t total = 0;

    mp3dec_init(&dec);
    while (pos + 4 < size) {
        int samples;
#ifdef UNIT_TEST
        if (g_probe_nsteps > 0) {
            if (g_probe_i >= g_probe_nsteps)
                break;
            samples          = g_probe_steps[g_probe_i].samples;
            info.frame_bytes = g_probe_steps[g_probe_i].frame_bytes;
            info.hz          = g_probe_steps[g_probe_i].hz;
            info.channels    = g_probe_steps[g_probe_i].channels;
            g_probe_i++;
        } else {
            samples = mp3dec_decode_frame(&dec, data + pos, (int)(size - pos), NULL, &info);
        }
#else
        samples = mp3dec_decode_frame(&dec, data + pos, (int)(size - pos), NULL, &info);
#endif
        if (info.frame_bytes <= 0)
            break;
        pos += (size_t)info.frame_bytes;
        if (samples > 0)
            total += samples;
        if (!found && samples > 0 && info.hz > 0 && info.channels > 0) {
            *channels = (unsigned int)info.channels;
            *sample_rate = (unsigned int)info.hz;
            found = 1;
        }
    }
    if (!found)
        return -1;
    *total_samples = total;
    return 0;
}

static void decode_thread(void *arg)
{
    mp3_ring_t *r = (mp3_ring_t *)arg;
    mp3d_sample_t *decode_buf = (mp3d_sample_t *)linearAlloc(PCM16_MAX_SAMPLES * sizeof(mp3d_sample_t));
    int16_t *pcm16 = (int16_t *)linearAlloc(PCM16_MAX_SAMPLES * sizeof(int16_t));
    size_t mp3_pos = r->read_pos;
    mp3dec_frame_info_t info;

#ifdef UNIT_TEST
    if (g_fail_decode_buf) {
        linearFree(decode_buf);
        decode_buf = NULL;
    }
    if (g_fail_pcm16) {
        linearFree(pcm16);
        pcm16 = NULL;
    }
#endif

    if (!decode_buf || !pcm16) {
        linearFree(decode_buf);
        linearFree(pcm16);
        r->decode_done = true;
        return;
    }

    mp3dec_init(&r->dec);

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
        if (g_decode_throttle_times > 0) {
            g_decode_throttle_times--;
            if (g_decode_throttle_ns > 0)
                svcSleepThread((s64)g_decode_throttle_ns);
        }
#endif
        {
            int full = pcm_ring_producer_full(r->write_pos, r->ring_read_pos, r->ring_size, r->chunk_bytes);
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
        }

        if (mp3_pos >= r->data_size) {
            r->decode_done = true;
            break;
        }

        int samples = mp3dec_decode_frame(&r->dec, r->data + mp3_pos,
                                            (int)(r->data_size - mp3_pos),
                                            decode_buf, &info);
#ifdef UNIT_TEST
        if (g_force_decode_frame_bytes_zero)
            info.frame_bytes = 0;
#endif
        if (info.frame_bytes <= 0)
            break;
        mp3_pos += (size_t)info.frame_bytes;
        r->read_pos = mp3_pos;

        if (samples <= 0)
            continue;

        {
            int pcm_count = samples * info.channels;
            size_t to_write = (size_t)pcm_count * sizeof(int16_t);

#ifdef UNIT_TEST
            if (g_decode_max_chunks > 0) {
                size_t limit = (size_t)g_decode_max_chunks * r->chunk_bytes;
                if (r->write_pos >= limit) {
                    r->decode_done = true;
                    break;
                }
                if (r->write_pos + to_write > limit)
                    to_write = limit - r->write_pos;
            }
#endif

            mp3dec_f32_to_s16(decode_buf, pcm16, pcm_count);
            pcm_ring_write(r->ring, r->ring_size, r->write_pos, pcm16, to_write);
            LightLock_Lock(&r->lock);
            r->write_pos += to_write;
            LightLock_Unlock(&r->lock);
            /* Next PCM write iteration hits write_pos >= limit and stops. */
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
    int64_t total_samples = 0;
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

    if (probe_mp3(file_data, file_size, data_start, &ch, &sr, &total_samples) != 0) {
        linearFree(file_data);
        return -2;
    }

#ifdef UNIT_TEST
    if (g_format_override) {
        ch = g_override_ch;
        sr = g_override_sr;
    }
#endif

    resume_samples = audio_take_resume_sample();
    resuming = (resume_samples >= 0);

    if (ch < 1 || ch > 2 || sr < 300 || sr > 48000) {
        linearFree(file_data);
        return -4;
    }

    if (total_samples > 0)
        audio_ctrl_set_duration(total_samples);
    audio_ctrl_set_sample_rate((int32_t)sr);
    audio_viz_reset();
    if (resuming)
        audio_ctrl_set_position(resume_samples);

    samples_fed = resuming ? (uint64_t)resume_samples : 0;

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
#ifdef UNIT_TEST
            if (g_force_skip_frame_bytes_zero)
                info.frame_bytes = 0;
#endif
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
        error_log_set_site("mp3:decode_thread");
        return -7;
    }

    ndsp_format = (ch == 2) ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16;
    ndspChnReset(channel_id);
    ndspChnSetInterp(channel_id, NDSP_INTERP_LINEAR);
    ndspChnSetRate(channel_id, (float)sr);
    ndspChnSetFormat(channel_id, ndsp_format);
    ndspChnSetMix(channel_id, (float[12]){ 1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });

    buf_bytes = chunk_bytes;
#ifdef UNIT_TEST
    if (g_force_decode_spin) {
        while (!g_decode_in_loop && !ring.decode_done)
            svcSleepThread(100000);
    }
#endif
    for (i = 0; i < N_WAVEBUFS; i++) {
        bufs[i] = linearAlloc(buf_bytes);
        if (!bufs[i]) {
            ring.stop = true;
            threadJoin(th, U64_MAX);
            threadFree(th);
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

    prefill = pcm_ring_prefill_target(resuming, chunk_bytes, sr, ch, ring.ring_size);
    while (!audio_playback_should_exit()) {
        size_t avail;
        LightLock_Lock(&ring.lock);
        avail = pcm_ring_avail(ring.write_pos, ring.ring_read_pos);
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
            avail = pcm_ring_avail(ring.write_pos, ring.ring_read_pos);
            LightLock_Unlock(&ring.lock);
            if (avail >= chunk_bytes) {
                pcm_ring_read(ring.ring, ring.ring_size, ring.ring_read_pos, bufs[next], chunk_bytes);
                LightLock_Lock(&ring.lock);
                ring.ring_read_pos += chunk_bytes;
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
                if (avail > 0) {
                    size_t frame_bytes = ch * sizeof(int16_t);
                    size_t samples;
                    size_t rp_snap;

                    LightLock_Lock(&ring.lock);
                    rp_snap = ring.ring_read_pos;
                    samples = pcm_ring_pop_partial(
                        ring.ring, ring.ring_size, ring.write_pos, &rp_snap,
                        bufs[next], chunk_bytes, frame_bytes);
                    ring.ring_read_pos = rp_snap;
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
            size_t a;
            LightLock_Lock(&ring.lock);
            a = pcm_ring_avail(ring.write_pos, ring.ring_read_pos);
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
    threadFree(th);
    ndspChnWaveBufClear(channel_id);

    /* Natural end: snap duration to what actually played so the final
     * position/duration ratio is exactly 1.0 even if the header-walk total
     * and the decoded total disagree (e.g. corrupt tail frames). */
    if (stream_done && !pausing && !audio_should_stop() && samples_fed > 0)
        audio_ctrl_set_duration((int64_t)samples_fed);

    if (pausing)
        audio_note_paused_at((int64_t)samples_fed);

    for (i = 0; i < N_WAVEBUFS; i++)
        linearFree(bufs[i]);
    linearFree(ring.ring);
    linearFree(file_data);
    return 0;
}
