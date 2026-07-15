#include "file_magic.h"
#include "path_util.h"

#include <stdio.h>
#include <string.h>

#define FLAC_MAGIC "fLaC"

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

audio_route_t audio_route_for_path(const char *path)
{
    if (!path)
        return AUDIO_ROUTE_VGM;
    if (path_is_flac(path) || file_has_flac_magic(path))
        return AUDIO_ROUTE_FLAC;
    if (path_is_mp3(path))
        return AUDIO_ROUTE_MP3;
    return AUDIO_ROUTE_VGM;
}
