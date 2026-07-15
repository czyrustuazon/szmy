#include "bmp_util.h"

static uint32_t u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t u16le(const uint8_t *p)
{
    return (uint32_t)(p[0] | (p[1] << 8));
}

int bmp_open(bmp_view_t *b, const uint8_t *data, uint32_t size)
{
    if (!b || !data || size < 54u || data[0] != 'B' || data[1] != 'M')
        return -1;

    uint32_t width   = u32le(data + 18);
    int32_t  iheight = (int32_t)u32le(data + 22);
    uint32_t height  = (uint32_t)(iheight < 0 ? -iheight : iheight);
    uint32_t comp    = u32le(data + 30);
    uint16_t bpp     = (uint16_t)u16le(data + 28);

    b->data     = data;
    b->size     = size;
    b->w        = width;
    b->h        = height;
    b->off      = u32le(data + 10);
    b->comp     = comp;
    b->bpp      = bpp;
    b->top_down = (iheight < 0) ? 1 : 0;
    b->row_b    = (bpp * width + 31u) / 32u * 4u;

    if (b->off + b->row_b * b->h > b->size)
        return -1;
    if (bpp != 24u && bpp != 32u)
        return -1;
    if (comp == 1u || comp == 2u || comp == 4u || comp == 5u)
        return -1;
    if (bpp == 24u && comp != 0u)
        return -1;
    if (bpp == 32u && comp != 0u && comp != 3u)
        return -1;
    return 0;
}

void bmp_bgra(bmp_view_t *b, uint32_t x, uint32_t y,
              uint8_t *B, uint8_t *G, uint8_t *R, uint8_t *A)
{
    uint32_t    yf  = b->top_down ? y : (b->h - 1u - y);
    const uint8_t *row = b->data + b->off + yf * b->row_b;

    if (b->bpp == 32u) {
        const uint8_t *p = row + x * 4u;
        *B = p[0];
        *G = p[1];
        *R = p[2];
        *A = p[3];
        if (b->comp == 0u) {
            if (*A < 2u) {
                uint32_t rgb = (uint32_t)(*R) + (uint32_t)(*G) + (uint32_t)(*B);
                *A = (rgb > 4u) ? 255u : 0u;
            }
        } else if (*A < 2u) {
            uint32_t rgb2 = (uint32_t)(*R) + (uint32_t)(*G) + (uint32_t)(*B);
            if (rgb2 > 20u)
                *A = 255u;
        }
    } else {
        const uint8_t *p = row + x * 3u;
        *B = p[0];
        *G = p[1];
        *R = p[2];
        *A = 255u;
    }
}

uint16_t bmp_mix565(uint16_t dst, uint8_t sr, uint8_t sg, uint8_t sb, uint32_t sa)
{
    if (sa < 1u)
        return dst;

    uint32_t r5 = (uint32_t)(dst >> 11) & 31u;
    uint32_t g6 = (dst >> 5) & 63u;
    uint32_t b5 = dst & 31u;
    uint32_t dr = (r5 << 3) | (r5 >> 2);
    uint32_t dg = (g6 << 2) | (g6 >> 4);
    uint32_t db = (b5 << 3) | (b5 >> 2);
    uint32_t inv = 255u - sa;
    uint32_t oR  = (sr * sa + dr * inv) / 255u;
    uint32_t oG  = (sg * sa + dg * inv) / 255u;
    uint32_t oB  = (sb * sa + db * inv) / 255u;
    uint32_t o5  = oR >> 3;
    uint32_t o6  = oG >> 2;
    uint32_t ob5 = oB >> 3;

    return (uint16_t)(o5 | (o6 << 5) | (ob5 << 11));
}

uint8_t bmp_alpha_key_24(uint8_t b, uint8_t g, uint8_t r)
{
    uint32_t t = (uint32_t)b + (uint32_t)g + (uint32_t)r;
    uint32_t m = (b > g) ? (b > r ? b : r) : (g > r ? g : r);

    if (t < 8u)
        return 0;
    if (m < 18u && t < 30u)
        return (uint8_t)(t * 8u);
    if (m < 40u)
        return (uint8_t)(80u + m * 3u);
    return 255u;
}

int bmp_point_in(int px, int py, int x0, int y0, int w, int h, int hit_pad)
{
    if (w <= 0 || h <= 0)
        return 0;
    return (px + hit_pad >= x0
            && px - hit_pad < x0 + w
            && py + hit_pad >= y0
            && py - hit_pad < y0 + h);
}
