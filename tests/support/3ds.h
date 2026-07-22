#pragma once

/* Host-test stub for libctru 3ds.h (shadows real header via -Isupport). */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int64_t   s64;

typedef struct PrintConsole PrintConsole;

typedef struct Thread *Thread;

typedef struct {
    int dummy;
} LightLock;

typedef struct {
    void  *data_vaddr;
    u32    nsamples;
    u32    status;
} ndspWaveBuf;

#define NDSP_WBUF_FREE    0u
#define NDSP_WBUF_DONE    1u
#define NDSP_WBUF_PLAYING 2u

#define NDSP_FORMAT_MONO_PCM16    0
#define NDSP_FORMAT_STEREO_PCM16  1
#define NDSP_INTERP_LINEAR        0
#define NDSP_OUTPUT_STEREO        0

#define U64_MAX  ((u64)-1)

void  *linearAlloc(size_t size);
void   linearFree(void *mem);

void   LightLock_Init(LightLock *lock);
void   LightLock_Lock(LightLock *lock);
void   LightLock_Unlock(LightLock *lock);

Thread threadCreate(void (*entry)(void *), void *arg, size_t stack_size,
                    int priority, int core_id, bool detached);
void   threadJoin(Thread thread, u64 timeout_ns);
void   threadFree(Thread thread);

void   svcSleepThread(u64 nanoseconds);

int    ndspInit(void);
void   ndspExit(void);
void   ndspSetOutputMode(int mode);

void   ndspChnReset(int channel);
void   ndspChnSetInterp(int channel, int interp);
void   ndspChnSetRate(int channel, float rate);
void   ndspChnSetFormat(int channel, int format);
void   ndspChnSetMix(int channel, float mix[12]);
void   ndspChnWaveBufAdd(int channel, ndspWaveBuf *buf);
void   ndspChnWaveBufClear(int channel);

void   DSP_FlushDataCache(const void *addr, size_t size);

/* Test helpers (host_mocks.c). */
void   host_mock_reset(void);
void   host_mock_set_thread_fail(int fail);
void   host_mock_set_alloc_fail_after(int n_success);
void   host_mock_set_wavebuf_hold(int hold_submissions);
void   host_mock_set_wavebuf_persist(int persist);
