#include "file_magic.h"
#include "path_util.h"

#include <stdio.h>
#include <string.h>

#define FLAC_MAGIC "fLaC"
#define OGG_MAGIC  "OggS"
#define OPUS_HEAD  "OpusHead"

int file_has_flac_magic(const char *path)
{
    FILE *f;

    if (!path)
        return 0;

    f = fopen(path, "rb");
    if (!f)
        return 0;

    char buf[4];
    int ok = (fread(buf, 1, 4, f) == 4 && memcmp(buf, FLAC_MAGIC, 4) == 0);
    fclose(f);
    return ok;
}

/* True when the file is an Ogg container with an OpusHead identification packet
 * in the first few kilobytes (covers .opus and some .ogg Opus files). */
int file_has_opus_magic(const char *path)
{
    FILE *f;
    unsigned char buf[4096];
    size_t n;
    size_t i;

    if (!path)
        return 0;

    f = fopen(path, "rb");
    if (!f)
        return 0;

    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n < 8 || memcmp(buf, OGG_MAGIC, 4) != 0)
        return 0;

    for (i = 0; i + 8 <= n; i++) {
        if (memcmp(buf + i, OPUS_HEAD, 8) == 0)
            return 1;
    }
    return 0;
}

audio_route_t audio_route_for_path(const char *path)
{
    if (!path)
        return AUDIO_ROUTE_VGM;
    if (path_is_flac(path) || file_has_flac_magic(path))
        return AUDIO_ROUTE_FLAC;
    if (path_is_mp3(path))
        return AUDIO_ROUTE_MP3;
    if (path_is_opus(path) || file_has_opus_magic(path))
        return AUDIO_ROUTE_OPUS;
    return AUDIO_ROUTE_VGM;
}
