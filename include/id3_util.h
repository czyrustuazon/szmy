#ifndef ID3_UTIL_H
#define ID3_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Bytes to skip before the first MPEG frame (0 if no ID3v2 tag). */
size_t id3_skip_tag(const uint8_t *data, size_t size);

#endif /* ID3_UTIL_H */
