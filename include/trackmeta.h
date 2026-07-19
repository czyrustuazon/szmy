#ifndef TRACKMETA_H
#define TRACKMETA_H

#include <stddef.h>
#include <stdint.h>

/* Tag reader for the now-playing panel: ID3v2 (MP3), FLAC Vorbis comments,
 * and OpusTags (Ogg Opus), including an embedded cover picture when present. */

#define TRACKMETA_TEXT_MAX 96
#define TRACKMETA_ART_MAX  (3u * 1024u * 1024u)

typedef struct {
    char title[TRACKMETA_TEXT_MAX];
    char artist[TRACKMETA_TEXT_MAX];
    char album[TRACKMETA_TEXT_MAX];
    char genre[48];
    char year[12];
    char track[12];
    uint8_t *art;      /* raw embedded picture (JPEG/PNG), malloc'd or NULL */
    size_t   art_size;
} trackmeta_t;

/* Fill *m from the file's tags (fields left empty when absent). Always
 * initializes *m. Returns 0 when the file was readable, -1 otherwise. */
int trackmeta_read(const char *path, trackmeta_t *m);

/* Release the embedded art buffer (safe on an empty struct). */
void trackmeta_free(trackmeta_t *m);

/* Decode raw embedded art (JPEG/PNG) and scale it to w*h RGB565 pixels in
 * out (row-major). Returns 0 on success. */
int trackmeta_decode_art(const uint8_t *data, size_t size,
                         uint16_t *out, int w, int h);

#endif /* TRACKMETA_H */
