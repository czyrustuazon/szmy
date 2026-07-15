#ifndef BMP_UTIL_H
#define BMP_UTIL_H

#include <stdint.h>

#define BMP_HIT_PAD_DEFAULT 6

typedef struct {
    const uint8_t *data;
    uint32_t       size;
    uint32_t       w;
    uint32_t       h;
    uint32_t       off;
    uint32_t       comp;
    uint16_t       bpp;
    int            top_down;
    uint32_t       row_b;
} bmp_view_t;

int      bmp_open(bmp_view_t *b, const uint8_t *data, uint32_t size);
void     bmp_bgra(bmp_view_t *b, uint32_t x, uint32_t y,
                  uint8_t *B, uint8_t *G, uint8_t *R, uint8_t *A);
uint16_t bmp_mix565(uint16_t dst, uint8_t sr, uint8_t sg, uint8_t sb, uint32_t sa);
uint8_t  bmp_alpha_key_24(uint8_t b, uint8_t g, uint8_t r);
int      bmp_point_in(int px, int py, int x0, int y0, int w, int h, int hit_pad);

#endif /* BMP_UTIL_H */
