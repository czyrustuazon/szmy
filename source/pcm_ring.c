#include "pcm_ring.h"

#include <string.h>

size_t pcm_ring_avail(size_t write_pos, size_t read_pos)
{
    return write_pos - read_pos;
}

int pcm_ring_producer_full(size_t write_pos, size_t read_pos, size_t cap, size_t chunk_reserve)
{
    size_t used = pcm_ring_avail(write_pos, read_pos);
    return used > cap - chunk_reserve;
}

void pcm_ring_write(uint8_t *buf, size_t cap, size_t write_pos, const void *src, size_t n)
{
    size_t w = write_pos % cap;

    if (w + n <= cap) {
        memcpy(buf + w, src, n);
    } else {
        size_t first = cap - w;
        memcpy(buf + w, src, first);
        memcpy(buf, (const uint8_t *)src + first, n - first);
    }
}

void pcm_ring_read(const uint8_t *buf, size_t cap, size_t read_pos, void *dst, size_t n)
{
    size_t rp = read_pos % cap;

    if (rp + n <= cap) {
        memcpy(dst, buf + rp, n);
    } else {
        size_t first = cap - rp;
        memcpy(dst, buf + rp, first);
        memcpy((uint8_t *)dst + first, buf, n - first);
    }
}

size_t pcm_ring_prefill_target(int resuming, size_t chunk_bytes, unsigned sample_rate,
                               unsigned channels, size_t cap)
{
    size_t prefill = resuming ? (chunk_bytes * 2u) : (size_t)(sample_rate * channels * 2u);

    if (prefill > cap / 2)
        prefill = cap / 2;
    return prefill;
}

size_t pcm_ring_samples_from_bytes(size_t avail_bytes, size_t frame_bytes)
{
    if (frame_bytes == 0)
        return 0;
    return avail_bytes / frame_bytes;
}

size_t pcm_ring_pop_partial(const uint8_t *buf, size_t cap, size_t write_pos, size_t *read_pos,
                            void *dst, size_t chunk_bytes, size_t frame_bytes)
{
    size_t avail = pcm_ring_avail(write_pos, *read_pos);
    size_t samples;
    size_t to_copy;

    if (avail == 0 || frame_bytes == 0)
        return 0;

    samples  = pcm_ring_samples_from_bytes(avail, frame_bytes);
    to_copy  = samples * frame_bytes;
    pcm_ring_read(buf, cap, *read_pos, dst, to_copy);
    memset((uint8_t *)dst + to_copy, 0, chunk_bytes - to_copy);
    *read_pos += to_copy;
    return samples;
}
