#include "host_opusfile.h"
#include "3ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct OggOpusFile {
    int channels;
    int64_t total;
    int64_t frames_read;
    int closed;
    int seek_fail;
    int hole_once;
    int bad_packet_once;
};

static OggOpusFile *host_opus_new(int ch, int64_t total)
{
    OggOpusFile *of = (OggOpusFile *)calloc(1, sizeof(OggOpusFile));
    if (!of)
        return NULL;
    of->channels = ch;
    of->total    = total;
    return of;
}

OggOpusFile *op_open_file(const char *path, int *error)
{
    if (error)
        *error = 0;
    if (!path) {
        if (error)
            *error = -1;
        return NULL;
    }
    if (strcmp(path, HOST_OPUS_FIXTURE) == 0)
        return host_opus_new(1, 5000);
    if (strcmp(path, "bad_channels.opus") == 0)
        return host_opus_new(6, 100);
    if (strcmp(path, "zero_channels.opus") == 0)
        return host_opus_new(0, 100);
    if (strcmp(path, "stereo.opus") == 0)
        return host_opus_new(2, 5000);
    if (strcmp(path, "slow_mono.opus") == 0)
        return host_opus_new(1, 60000);
    if (strcmp(path, "exact4.opus") == 0)
        return host_opus_new(1, 16384);
    if (strcmp(path, "over4.opus") == 0)
        return host_opus_new(1, 20480);
    if (strcmp(path, "long_mono.opus") == 0)
        return host_opus_new(1, 50000);
    if (strcmp(path, "seek_fail.opus") == 0) {
        OggOpusFile *of = host_opus_new(1, 5000);
        if (of)
            of->seek_fail = 1;
        return of;
    }
    if (strcmp(path, "hole_then_data.opus") == 0) {
        OggOpusFile *of = host_opus_new(1, 5000);
        if (of)
            of->hole_once = 1;
        return of;
    }
    if (strcmp(path, "bad_packet.opus") == 0) {
        OggOpusFile *of = host_opus_new(1, 5000);
        if (of)
            of->bad_packet_once = 1;
        return of;
    }
    if (strcmp(path, "unknown_total.opus") == 0) {
        OggOpusFile *of = host_opus_new(1, 4096);
        if (of)
            of->total = -1;
        return of;
    }
    {
        FILE *f = fopen(path, "rb");
        if (!f) {
            if (error)
                *error = -1;
            return NULL;
        }
        char magic[4];
        if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "OggS", 4) != 0) {
            fclose(f);
            if (error)
                *error = -1;
            return NULL;
        }
        fclose(f);
        return host_opus_new(2, 8000);
    }
}

void op_free(OggOpusFile *of)
{
    if (of)
        of->closed = 1;
    free(of);
}

int op_channel_count(const OggOpusFile *of, int li)
{
    (void)li;
    if (!of)
        return 0;
    return of->channels;
}

ogg_int64_t op_pcm_total(const OggOpusFile *of, int li)
{
    (void)li;
    if (!of)
        return -1;
    return of->total;
}

int op_pcm_seek(OggOpusFile *of, int64_t pcm_offset)
{
    if (!of || of->seek_fail)
        return -1;
    if (of->total >= 0 && pcm_offset > of->total)
        return -1;
    of->frames_read = pcm_offset;
    return 0;
}

int op_read(OggOpusFile *of, int16_t *pcm, int buf_size, int *li)
{
    int64_t left;
    int max_frames;
    int frames;
    int i;

    (void)li;
    if (!of || of->closed || !pcm || buf_size <= 0 || of->channels < 1)
        return 0;

    if (of->hole_once) {
        of->hole_once = 0;
        return OP_HOLE;
    }
    if (of->bad_packet_once) {
        of->bad_packet_once = 0;
        return OP_EBADPACKET;
    }

    if (of->total < 0)
        left = 4096 - of->frames_read;
    else
        left = of->total - of->frames_read;
    if (left <= 0)
        return 0;

    max_frames = buf_size / of->channels;
    if (max_frames <= 0)
        return 0;
    frames = max_frames;
    if ((int64_t)frames > left)
        frames = (int)left;

    if (of->total >= 60000 && frames > 1024) {
        frames = 1024;
        svcSleepThread(2000000);
    }

    for (i = 0; i < frames * of->channels; i++)
        pcm[i] = (int16_t)(i & 1 ? 100 : -100);

    of->frames_read += frames;
    return frames;
}
