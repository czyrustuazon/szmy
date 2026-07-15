#include "id3_util.h"

#include <string.h>

size_t id3_skip_tag(const uint8_t *data, size_t size)
{
    if (size < 10 || memcmp(data, "ID3", 3) != 0)
        return 0;

    size_t tag_size = ((size_t)(data[6] & 0x7f) << 21) |
                      ((size_t)(data[7] & 0x7f) << 14) |
                      ((size_t)(data[8] & 0x7f) << 7) |
                      (size_t)(data[9] & 0x7f);
    return 10 + tag_size;
}
