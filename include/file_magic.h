#ifndef FILE_MAGIC_H
#define FILE_MAGIC_H

typedef enum {
    AUDIO_ROUTE_VGM  = 0,
    AUDIO_ROUTE_FLAC = 1,
    AUDIO_ROUTE_MP3  = 2,
    AUDIO_ROUTE_OPUS = 3,
} audio_route_t;

int           file_has_flac_magic(const char *path);
int           file_has_opus_magic(const char *path);
audio_route_t audio_route_for_path(const char *path);

#endif /* FILE_MAGIC_H */
