#pragma once

/* Minimal dr_flac stand-in for host tests (flac_player.c with UNIT_TEST). */

#include <stddef.h>
#include <stdint.h>

typedef int16_t  drflac_int16;
typedef uint64_t drflac_uint64;

typedef struct {
    unsigned int channels;
    unsigned int sampleRate;
    drflac_uint64 totalFrames;
    drflac_uint64 framesRead;
    int          closed;
} drflac;

drflac *drflac_open_file(const char *path, void *userdata);
void    drflac_close(drflac *flac);
drflac_uint64 drflac_read_pcm_frames_s16(drflac *flac, drflac_uint64 frames, drflac_int16 *buffer);
int     drflac_seek_to_pcm_frame(drflac *flac, drflac_uint64 frame);

#define HOST_FLAC_FIXTURE "host_test.flac"

void flac_test_set_fake_producer_full(int times);
void flac_test_set_decode_spin(int spin);
void flac_test_set_decode_spin_stop_after(int iters);
