#include "host_drflac.h"
#include "3ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static drflac *host_flac_new(unsigned ch, unsigned sr, drflac_uint64 total)
{
    drflac *f = (drflac *)calloc(1, sizeof(drflac));
    if (!f)
        return NULL;
    f->channels    = ch;
    f->sampleRate  = sr;
    f->totalFrames = total;
    return f;
}

drflac *drflac_open_file(const char *path, void *userdata)
{
    (void)userdata;
    if (!path)
        return NULL;
    if (strcmp(path, HOST_FLAC_FIXTURE) == 0)
        return host_flac_new(1, 44100, 5000);
    if (strcmp(path, "bad_channels.flac") == 0)
        return host_flac_new(6, 44100, 100);
    if (strcmp(path, "zero_channels.flac") == 0)
        return host_flac_new(0, 44100, 100);
    if (strcmp(path, "bad_rate.flac") == 0)
        return host_flac_new(1, 100, 100);
    if (strcmp(path, "high_rate.flac") == 0)
        return host_flac_new(1, 96000, 100);
    /* Inclusive boundaries of the accepted sample-rate range. */
    if (strcmp(path, "rate_low_ok.flac") == 0)
        return host_flac_new(1, 300, 100);
    if (strcmp(path, "rate_high_ok.flac") == 0)
        return host_flac_new(1, 48000, 100);
    if (strcmp(path, "stereo.flac") == 0)
        return host_flac_new(2, 44100, 5000);
    if (strcmp(path, "slow_mono.flac") == 0)
        return host_flac_new(1, 44100, 60000);
    /* Exactly four decode chunks (4096 frames each) for busy-buffer drain tests. */
    if (strcmp(path, "exact4.flac") == 0)
        return host_flac_new(1, 44100, 16384);
    /* Fifth chunk still in ring when next wraps to held buf0 → busy-buffer yield. */
    if (strcmp(path, "over4.flac") == 0)
        return host_flac_new(1, 44100, 20480);
    /* Longer than prefill threshold so playback starts before decode finishes. */
    if (strcmp(path, "long_mono.flac") == 0)
        return host_flac_new(1, 44100, 50000);
    {
        FILE *f = fopen(path, "rb");
        if (!f)
            return NULL;
        char magic[4];
        if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "fLaC", 4) != 0) {
            fclose(f);
            return NULL;
        }
        fclose(f);
        return host_flac_new(2, 48000, 8000);
    }
}

void drflac_close(drflac *flac)
{
    if (flac)
        flac->closed = 1;
    free(flac);
}

drflac_uint64 drflac_read_pcm_frames_s16(drflac *flac, drflac_uint64 frames, drflac_int16 *buffer)
{
    drflac_uint64 left;
    drflac_uint64 i;

    if (!flac || flac->closed || !buffer || frames == 0)
        return 0;

    left = flac->totalFrames - flac->framesRead;
    if (frames > left)
        frames = left;
    if (frames == 0)
        return 0;

    /* Throttle long fixtures so playback waits for data (avail < chunk). */
    if (flac->totalFrames >= 60000u && frames > 1024u) {
        frames = 1024u;
        svcSleepThread(2000000);
    }

    for (i = 0; i < frames * flac->channels; i++)
        buffer[i] = (drflac_int16)(i & 1 ? 100 : -100);

    flac->framesRead += frames;
    return frames;
}

int drflac_seek_to_pcm_frame(drflac *flac, drflac_uint64 frame)
{
    if (!flac || frame > flac->totalFrames)
        return 0;
    flac->framesRead = frame;
    return 1;
}
