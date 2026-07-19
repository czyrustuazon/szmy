/* Equalizer visualizer levels.
 * Goertzel magnitudes at log-spaced frequencies over a short window of the
 * chunk being fed to NDSP. Cheap enough for O3DS (~6k float MACs per chunk,
 * i.e. per ~40-90 ms of audio). Single writer (playback thread), single
 * reader (UI thread); levels are independent bytes so no lock is needed. */

#include "audio_viz.h"

#include <math.h>

#define VIZ_WINDOW   512   /* analysis frames per feed */
#define VIZ_FMIN     80.f  /* lowest band center (Hz) */
#define VIZ_FMAX_CAP 12000.f
#define VIZ_TWO_PI   6.28318530717958647692f
#define VIZ_GAIN     6.f   /* music bands sit well below 0 dBFS; boost them */
#define VIZ_SCOPE_GAIN 3.f /* waveform boost so quiet passages still wiggle */

/* Waveform stream storage: ring of decimated points (~0.4 s of audio) and
 * the most points a single chunk can contribute (4096-frame chunk → 256,
 * plus decimation carry). */
#define VIZ_SCOPE_RING     1024
#define VIZ_CHUNK_PTS_MAX  288

typedef struct {
    uint8_t bars[AUDIO_VIZ_BARS];
    int8_t  pts[VIZ_CHUNK_PTS_MAX]; /* waveform points of this chunk */
    int     npts;
} viz_frame_t;

static volatile uint8_t  s_bars[AUDIO_VIZ_BARS];
static volatile int8_t   s_scope_ring[VIZ_SCOPE_RING];
static volatile uint32_t s_scope_total; /* points published (monotonic) */
static volatile int      s_scope_mode;  /* 0 = bars active, 1 = waveform */
static int   s_coeff_sr;               /* sample rate the coeffs were built for */
static float s_coeff[AUDIO_VIZ_BARS];  /* Goertzel coefficients per band */

/* Decimation carry across chunks (chunks are fed sequentially). */
static int   s_decim_count;
static float s_decim_peak;

/* Chunks analyzed but not yet audible (still queued behind other wavebufs). */
static viz_frame_t s_pending[AUDIO_VIZ_DELAY];
static int         s_pending_n;

void audio_viz_set_scope_mode(int scope_on)
{
    s_scope_mode = (scope_on != 0);
}

void audio_viz_reset(void)
{
    int i;
    for (i = 0; i < AUDIO_VIZ_BARS; i++)
        s_bars[i] = 0;
    for (i = 0; i < VIZ_SCOPE_RING; i++)
        s_scope_ring[i] = 0;
    s_scope_total = 0;
    s_decim_count = 0;
    s_decim_peak  = 0.f;
    s_coeff_sr    = 0;
    s_pending_n   = 0;
}

static uint8_t fall_step(uint8_t v)
{
    unsigned int drop = (unsigned int)v / 2u + 8u;
    return (v > drop) ? (uint8_t)(v - drop) : 0;
}

static void rebuild_coeffs(int sample_rate)
{
    float fmax = (float)sample_rate * 0.45f;
    int   i;

    if (fmax > VIZ_FMAX_CAP)
        fmax = VIZ_FMAX_CAP;
    if (fmax < VIZ_FMIN * 1.5f)
        fmax = VIZ_FMIN * 1.5f;

    for (i = 0; i < AUDIO_VIZ_BARS; i++) {
        float t = (float)i / (float)(AUDIO_VIZ_BARS - 1);
        float f = VIZ_FMIN * powf(fmax / VIZ_FMIN, t);
        s_coeff[i] = 2.f * cosf(VIZ_TWO_PI * f / (float)sample_rate);
    }
    s_coeff_sr = sample_rate;
}

/* Make a delayed frame visible: this chunk is now what the speakers play. */
static void publish_frame(const viz_frame_t *f)
{
    uint32_t base = s_scope_total;
    int      i;

    for (i = 0; i < AUDIO_VIZ_BARS; i++) {
        if ((int)f->bars[i] >= (int)s_bars[i])
            s_bars[i] = f->bars[i];
        else
            s_bars[i] = fall_step(s_bars[i]);
    }
    /* Write points before bumping the total so the reader never sees an
     * index whose data has not landed yet. */
    for (i = 0; i < f->npts; i++)
        s_scope_ring[(base + (uint32_t)i) % VIZ_SCOPE_RING] = f->pts[i];
    s_scope_total = base + (uint32_t)f->npts;
}

void audio_viz_feed(const int16_t *pcm, size_t frames,
                    unsigned int channels, int sample_rate)
{
    viz_frame_t fresh;
    int         i, b;

    if (pcm == NULL || frames == 0 || sample_rate <= 0
        || channels < 1u || channels > 2u)
        return;

    /* Only the active display is computed; the other stays at its last
     * published state (bars fall to zero, scope stops advancing) and fills
     * back in over a few chunks after a switch. */
    fresh.npts = 0;
    for (b = 0; b < AUDIO_VIZ_BARS; b++)
        fresh.bars[b] = 0;

    if (s_scope_mode) {
        /* Waveform stream: peak-preserving decimation of the whole chunk
         * into one point per AUDIO_VIZ_SCOPE_DECIM samples, boosted so
         * typical music fills the small display. Carry spans chunk
         * boundaries so the point rate stays exact regardless of size. */
        for (i = 0; i < (int)frames && fresh.npts < VIZ_CHUNK_PTS_MAX; i++) {
            float v = (channels == 2u)
                ? ((float)pcm[2 * i] + (float)pcm[2 * i + 1]) * 0.5f
                : (float)pcm[i];
            if ((v >= 0.f ? v : -v) > (s_decim_peak >= 0.f ? s_decim_peak
                                                           : -s_decim_peak))
                s_decim_peak = v;
            if (++s_decim_count >= AUDIO_VIZ_SCOPE_DECIM) {
                float sv = s_decim_peak * (VIZ_SCOPE_GAIN * 127.f / 32768.f);
                sv = (sv > 127.f) ? 127.f : sv;
                sv = (sv < -127.f) ? -127.f : sv;
                fresh.pts[fresh.npts++] = (int8_t)sv;
                s_decim_count = 0;
                s_decim_peak  = 0.f;
            }
        }
    } else {
        float mono[VIZ_WINDOW];
        int   m = (frames < (size_t)VIZ_WINDOW) ? (int)frames : VIZ_WINDOW;

        if (sample_rate != s_coeff_sr)
            rebuild_coeffs(sample_rate);

        if (channels == 2u) {
            for (i = 0; i < m; i++)
                mono[i] = ((float)pcm[2 * i] + (float)pcm[2 * i + 1]) * 0.5f;
        } else {
            for (i = 0; i < m; i++)
                mono[i] = (float)pcm[i];
        }

        for (b = 0; b < AUDIO_VIZ_BARS; b++) {
            float coeff = s_coeff[b];
            float s1 = 0.f, s2 = 0.f;
            float power, amp, level;
            int   v;

            for (i = 0; i < m; i++) {
                float s0 = mono[i] + coeff * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
            power = (power < 0.f) ? 0.f : power; /* float rounding guard */
            /* Tone amplitude ≈ 2*sqrt(power)/m, normalized to s16 full
             * scale. Real music puts only a fraction of full scale into any
             * one band, so apply a fixed gain before the perceptual sqrt
             * curve. */
            amp   = 2.f * sqrtf(power) / ((float)m * 32768.f);
            amp  *= VIZ_GAIN;
            amp   = (amp > 1.f) ? 1.f : amp;
            level = sqrtf(amp);
            v     = (int)(level * 255.f);
            v     = (v > 255) ? 255 : v; /* float rounding guard */
            fresh.bars[b] = (uint8_t)v;
        }
    }

    /* This chunk sits behind AUDIO_VIZ_DELAY already-queued wavebufs; hold
     * it and publish the chunk that is starting to play right now. */
    if (s_pending_n == AUDIO_VIZ_DELAY) {
        publish_frame(&s_pending[0]);
        for (i = 1; i < AUDIO_VIZ_DELAY; i++)
            s_pending[i - 1] = s_pending[i];
        s_pending_n--;
    }
    s_pending[s_pending_n++] = fresh;
}

void audio_viz_read(uint8_t out[AUDIO_VIZ_BARS])
{
    int i;
    for (i = 0; i < AUDIO_VIZ_BARS; i++)
        out[i] = s_bars[i];
}

uint32_t audio_viz_scope_total(void)
{
    return s_scope_total;
}

void audio_viz_scope_window(uint32_t end_total, int8_t out[AUDIO_VIZ_SCOPE])
{
    uint32_t total = s_scope_total;
    int      i;

    if (end_total > total)
        end_total = total;

    for (i = 0; i < AUDIO_VIZ_SCOPE; i++) {
        int64_t j = (int64_t)end_total - AUDIO_VIZ_SCOPE + i;
        if (j < 0 || (int64_t)total - j > VIZ_SCOPE_RING)
            out[i] = 0; /* before start of stream, or already overwritten */
        else
            out[i] = s_scope_ring[(uint32_t)j % VIZ_SCOPE_RING];
    }
}
