#ifndef PCM_RING_H
#define PCM_RING_H

#include <stddef.h>
#include <stdint.h>

/* Monotonic byte counters index into a fixed-size ring (pos % cap). */

size_t pcm_ring_avail(size_t write_pos, size_t read_pos);

/* True when a producer should wait before writing another chunk. */
int pcm_ring_producer_full(size_t write_pos, size_t read_pos, size_t cap, size_t chunk_reserve);

/* Copy n bytes into the ring at write_pos (may wrap). Does not advance write_pos. */
void pcm_ring_write(uint8_t *buf, size_t cap, size_t write_pos, const void *src, size_t n);

/* Copy n bytes from the ring at read_pos (may wrap). */
void pcm_ring_read(const uint8_t *buf, size_t cap, size_t read_pos, void *dst, size_t n);

/* Pre-fill threshold used before playback starts. */
size_t pcm_ring_prefill_target(int resuming, size_t chunk_bytes, unsigned sample_rate,
                               unsigned channels, size_t cap);

size_t pcm_ring_samples_from_bytes(size_t avail_bytes, size_t frame_bytes);

/* Drain remaining bytes, zero-pad to chunk_bytes; returns PCM sample count. */
size_t pcm_ring_pop_partial(const uint8_t *buf, size_t cap, size_t write_pos, size_t *read_pos,
                            void *dst, size_t chunk_bytes, size_t frame_bytes);

#endif /* PCM_RING_H */
