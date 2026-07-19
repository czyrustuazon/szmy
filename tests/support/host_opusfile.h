#pragma once

/* Minimal libopusfile stand-in for host tests (opus_player.c with UNIT_TEST). */

#include <stddef.h>
#include <stdint.h>

typedef struct OggOpusFile OggOpusFile;
typedef int64_t ogg_int64_t;

#define OP_HOLE       (-3)
#define OP_EBADPACKET (-13)

OggOpusFile *op_open_file(const char *path, int *error);
void         op_free(OggOpusFile *of);
int          op_channel_count(const OggOpusFile *of, int li);
ogg_int64_t  op_pcm_total(const OggOpusFile *of, int li);
int          op_pcm_seek(OggOpusFile *of, int64_t pcm_offset);
int          op_read(OggOpusFile *of, int16_t *pcm, int buf_size, int *li);

#define HOST_OPUS_FIXTURE "host_test.opus"

void opus_test_set_fake_producer_full(int times);
void opus_test_set_decode_spin(int spin);
void opus_test_set_decode_spin_stop_after(int iters);
