#include "path_util.h"
#include "music_paths.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

int path_has_extension(const char *path, const char *ext)
{
    const char *dot;

    if (!path || !ext || ext[0] == '\0')
        return 0;

    dot = strrchr(path, '.');
    if (!dot || dot[1] == '\0')
        return 0;

    return strcasecmp(dot, ext) == 0;
}

int path_is_audio_extension(const char *name)
{
    const char *dot;

    if (!name)
        return 0;

    dot = strrchr(name, '.');
    if (dot == NULL || dot[1] == '\0')
        return 0;
    dot++;

    static const char *exts[] = {
        "wav", "flac", "mp3", "ogg", "opus", "aac", "m4a",
        "brstm", "bcwav", "bcstm", "bfstm", "bfwav", "sap",
        "sbc", "adx", "hca", "at9", "idsp", "dsp", "fsb",
        NULL
    };

    for (int i = 0; exts[i]; i++) {
        if (strcasecmp(dot, exts[i]) == 0)
            return 1;
    }
    return 0;
}

int path_is_flac(const char *path)
{
    return path_has_extension(path, ".flac");
}

int path_is_mp3(const char *path)
{
    return path_has_extension(path, ".mp3") || path_has_extension(path, ".mp2");
}

int path_is_opus(const char *path)
{
    return path_has_extension(path, ".opus");
}

void musiclist_format_cwd_label(const char *cwd, char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return;

    if (!cwd) {
        out[0] = '\0';
        return;
    }

    if (strncmp(cwd, FS_ROOT_FS, strlen(FS_ROOT_FS)) != 0) {
        snprintf(out, out_sz, "%s", cwd);
        return;
    }
    if (strcmp(cwd, FS_ROOT_FS) == 0) {
        snprintf(out, out_sz, "%s", FS_ROOT_LABEL);
        return;
    }
    /* sdmc:/music → Root:/music */
    snprintf(out, out_sz, "%s%s", FS_ROOT_LABEL, cwd + strlen(FS_ROOT_FS));
}
