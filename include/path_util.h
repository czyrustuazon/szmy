#ifndef PATH_UTIL_H
#define PATH_UTIL_H

#include <stddef.h>

/* Case-insensitive extension match on the final path component. */
int path_has_extension(const char *path, const char *ext);

/* True when the filename ends with a supported audio extension. */
int path_is_audio_extension(const char *filename);

int path_is_flac(const char *path);
int path_is_mp3(const char *path);
int path_is_opus(const char *path);

/* Format sdmc: cwd for UI (Root:/...). */
void musiclist_format_cwd_label(const char *cwd, char *out, size_t out_sz);

#endif /* PATH_UTIL_H */
