#include "3ds.h"
#include "host_pcm.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#include <time.h>
#endif

static int      g_thread_fail;
static int      g_alloc_fail_after = -1;
static int      g_alloc_count;
static int      g_wavebuf_hold;
static int      g_wavebuf_persist;
static ndspWaveBuf *g_playing_bufs[16];
static int      g_playing_count;

static int      g_pcm_format = -1;
static float    g_pcm_rate;
static uint64_t g_pcm_samples;
static unsigned g_pcm_bufs;
static uint32_t g_pcm_last_nsamples;

typedef struct {
    void (*entry)(void *);
    void  *arg;
} host_thread_args;

void host_pcm_reset(void)
{
    g_pcm_format        = -1;
    g_pcm_rate          = 0.0f;
    g_pcm_samples       = 0;
    g_pcm_bufs          = 0;
    g_pcm_last_nsamples = 0;
}

uint64_t host_pcm_samples_fed(void)
{
    return g_pcm_samples;
}

unsigned host_pcm_buffer_submissions(void)
{
    return g_pcm_bufs;
}

uint32_t host_pcm_last_chunk_samples(void)
{
    return g_pcm_last_nsamples;
}

int host_pcm_ndsp_format(void)
{
    return g_pcm_format;
}

float host_pcm_sample_rate(void)
{
    return g_pcm_rate;
}

unsigned host_pcm_channels(void)
{
    if (g_pcm_format == NDSP_FORMAT_STEREO_PCM16)
        return 2;
    if (g_pcm_format == NDSP_FORMAT_MONO_PCM16)
        return 1;
    return 0;
}

void host_mock_reset(void)
{
    g_thread_fail      = 0;
    g_alloc_fail_after = -1;
    g_alloc_count      = 0;
    g_wavebuf_hold     = 0;
    g_wavebuf_persist  = 0;
    g_playing_count    = 0;
    host_pcm_reset();
}

void host_mock_set_thread_fail(int fail)
{
    g_thread_fail = fail;
}

void host_mock_set_alloc_fail_after(int n_success)
{
    g_alloc_fail_after = n_success;
    g_alloc_count      = 0;
}

void host_mock_set_wavebuf_hold(int hold_submissions)
{
    g_wavebuf_hold = hold_submissions;
}

void host_mock_set_wavebuf_persist(int persist)
{
    g_wavebuf_persist = persist;
}

static void host_mock_release_one_playing(void)
{
    if (g_playing_count <= 0)
        return;
    g_playing_bufs[0]->status = NDSP_WBUF_DONE;
    for (int i = 1; i < g_playing_count; i++)
        g_playing_bufs[i - 1] = g_playing_bufs[i];
    g_playing_count--;
}

void *linearAlloc(size_t size)
{
    if (g_alloc_fail_after >= 0 && g_alloc_count++ >= g_alloc_fail_after)
        return NULL;
    return malloc(size);
}

void linearFree(void *mem)
{
    free(mem);
}

void LightLock_Init(LightLock *lock)
{
    if (lock)
        lock->dummy = 0;
}

void LightLock_Lock(LightLock *lock)
{
    (void)lock;
}

void LightLock_Unlock(LightLock *lock)
{
    (void)lock;
}

static void *host_thread_trampoline(void *p)
{
    host_thread_args *a = (host_thread_args *)p;
    a->entry(a->arg);
    free(a);
    return NULL;
}

Thread threadCreate(void (*entry)(void *), void *arg, size_t stack_size,
                    int priority, int core_id, bool detached)
{
    pthread_t *pt;
    host_thread_args *a;

    (void)stack_size;
    (void)priority;
    (void)core_id;
    (void)detached;

    if (g_thread_fail)
        return NULL;

    pt = (pthread_t *)malloc(sizeof(pthread_t));
    a  = (host_thread_args *)malloc(sizeof(host_thread_args));
    if (!pt || !a) {
        free(pt);
        free(a);
        return NULL;
    }

    a->entry = entry;
    a->arg   = arg;
    if (pthread_create(pt, NULL, host_thread_trampoline, a) != 0) {
        free(pt);
        free(a);
        return NULL;
    }
    return (Thread)pt;
}

void threadJoin(Thread thread, u64 timeout_ns)
{
    (void)timeout_ns;
    if (thread) {
        pthread_join(*(pthread_t *)thread, NULL);
        free(thread);
    }
}

void svcSleepThread(u64 nanoseconds)
{
    /* Must actually sleep/yield: a no-op lets decode busy-spin on a full ring
     * and starve the consumer thread under the host scheduler. */
#ifdef _WIN32
    if (nanoseconds == 0)
        SwitchToThread();
    else
        Sleep(1);
#else
    if (nanoseconds == 0) {
        sched_yield();
    } else {
        struct timespec ts = {0, 1000000L}; /* 1 ms */
        nanosleep(&ts, NULL);
    }
#endif
    /* Only the consumer's PLAYBACK_YIELD_US (~8000) may release held buffers.
     * Decode throttle sleeps are longer; releasing there cleared PLAYING before
     * next wrapped back, so decode_done==false on the busy path never ran. */
    if (!g_wavebuf_persist && g_playing_count > 0 && nanoseconds <= 8000ull)
        host_mock_release_one_playing();
}

int ndspInit(void)
{
    return 0;
}

void ndspExit(void) {}

void ndspSetOutputMode(int mode)
{
    (void)mode;
}

void ndspChnReset(int channel)
{
    (void)channel;
}

void ndspChnSetInterp(int channel, int interp)
{
    (void)channel;
    (void)interp;
}

void ndspChnSetRate(int channel, float rate)
{
    if (channel == 0)
        g_pcm_rate = rate;
}

void ndspChnSetFormat(int channel, int format)
{
    if (channel == 0)
        g_pcm_format = format;
}

void ndspChnSetMix(int channel, float mix[12])
{
    (void)channel;
    (void)mix;
}

void ndspChnWaveBufAdd(int channel, ndspWaveBuf *buf)
{
    (void)channel;
    if (!buf || !buf->data_vaddr || buf->nsamples == 0)
        return;

    g_pcm_samples += buf->nsamples;
    g_pcm_bufs++;
    g_pcm_last_nsamples = buf->nsamples;
    if (g_wavebuf_hold > 0) {
        buf->status = NDSP_WBUF_PLAYING;
        if (g_playing_count < (int)(sizeof(g_playing_bufs) / sizeof(g_playing_bufs[0])))
            g_playing_bufs[g_playing_count++] = buf;
        g_wavebuf_hold--;
    } else {
        buf->status = NDSP_WBUF_DONE;
    }
}

void ndspChnWaveBufClear(int channel)
{
    (void)channel;
}

void DSP_FlushDataCache(const void *addr, size_t size)
{
    (void)addr;
    (void)size;
}
