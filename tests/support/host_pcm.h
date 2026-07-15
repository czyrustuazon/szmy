#pragma once

#include <stdint.h>

/* PCM captured from ndspChnWaveBufAdd during host player tests. */

void     host_pcm_reset(void);
uint64_t host_pcm_samples_fed(void);
unsigned host_pcm_buffer_submissions(void);
uint32_t host_pcm_last_chunk_samples(void);
int      host_pcm_ndsp_format(void);
float    host_pcm_sample_rate(void);
unsigned host_pcm_channels(void);
