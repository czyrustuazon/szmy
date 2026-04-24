#include "topbg.h"
#include <3ds/gfx.h>
#include <3ds/services/gspgpu.h>
#include <3ds.h>
#include <string.h>
#include "top_screen_bg_bmp.h"

/* Top: 400x240 logical; libctru framebuffer is 240*400, RGB565, per console.c indexing. */
#define TOP_LOG_W 400
#define TOP_LOG_H 240

static int s_ok;

static u32 u32le(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u32 u16le(const u8 *p) { return (u32)(p[0] | (p[1] << 8)); }

/* Pixel (x,y) with y=0 at top, x in [0,sw), y in [0,sh) */
static void bmp_bgr24_at(
    const u8 *bmp, u32 off_bits, u32 width, u32 height, int top_down, u32 row_bytes,
    u32 x, u32 y, u8 *b, u8 *g, u8 *r) {
    u32 y_file = top_down ? y : (height - 1u - y);
    const u8 *row   = bmp + off_bits + y_file * row_bytes;
    const u8 *px    = row + x * 3u;
    *b = px[0];
    *g = px[1];
    *r = px[2];
}

static void bmp_bgr32_at(
    const u8 *bmp, u32 off_bits, u32 width, u32 height, int top_down, u32 row_bytes,
    u32 x, u32 y, u8 *b, u8 *g, u8 *r) {
    u32 y_file = top_down ? y : (height - 1u - y);
    const u8 *row = bmp + off_bits + y_file * row_bytes;
    const u8 *px  = row + x * 4u;
    *b = px[0];
    *g = px[1];
    *r = px[2];
}

int topbg_init(void) {
    const u8 *data = top_screen_bg_bmp;
    size_t      z    = top_screen_bg_bmp_size;

    if (z < 54u) {
        s_ok = 0;
        return -1;
    }
    if (data[0] != 'B' || data[1] != 'M') {
        s_ok = 0;
        return -1;
    }

    u32 off_bits  = u32le(data + 10);
    s32 ihead_sz  = (s32)u32le(data + 14);
    u32 width     = u32le(data + 18);
    s32 iheight   = (s32)u32le(data + 22);
    u16 bpp       = (u16)u16le(data + 28);

    if (ihead_sz < 40 || width < 1u || iheight == 0 || (bpp != 24u && bpp != 32u)) {
        s_ok = 0;
        return -1;
    }

    u32 height   = (u32)(iheight < 0 ? -iheight : iheight);
    u32 row_bytes = ((width * (u32)bpp + 31u) / 32u) * 4u;
    u32 need      = off_bits + row_bytes * height;
    if (need > z) {
        s_ok = 0;
        return -1;
    }

    (void)ihead_sz;
    s_ok = 1;
    return 0;
}

void topbg_blit_to_top(PrintConsole *top) {
    u16 *fb;

    if (!s_ok || !top)
        return;

    if (gfxGetScreenFormat(GFX_TOP) != GSP_RGB565_OES) {
        /* consoleInit() switches top to RGB565; if that changes, skip. */
        return;
    }

    /*
     * PrintConsole draws with the pointer stored at init (top->frameBuffer).
     * gfxGetFramebuffer() can disagree; we must blit to the same buffer the console uses.
     */
    fb = (u16 *)top->frameBuffer;
    if (!fb)
        fb = (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    if (!fb)
        return;

    const u8 *data = top_screen_bg_bmp;
    u32      off_bits  = u32le(data + 10);
    u32      width     = u32le(data + 18);
    s32      iheight   = (s32)u32le(data + 22);
    u16      bpp       = (u16)u16le(data + 28);
    u32      height    = (u32)(iheight < 0 ? -iheight : iheight);
    int      top_down  = iheight < 0;
    u32      row_bytes = ((width * (u32)bpp + 31u) / 32u) * 4u;

    u32 x_max = width  > 0u ? width  - 1u : 0u;
    u32 y_max = height > 0u ? height - 1u : 0u;

    for (u32 dy = 0; dy < TOP_LOG_H; dy++) {
        for (u32 dx = 0; dx < TOP_LOG_W; dx++) {
            u32 sx = (dx * x_max) / (TOP_LOG_W - 1u);
            u32 sy = (dy * y_max) / (TOP_LOG_H - 1u);
            u8  bb, gg, rr;
            if (bpp == 32u) {
                bmp_bgr32_at(data, off_bits, width, height, top_down, row_bytes, sx, sy, &bb, &gg, &rr);
            } else {
                bmp_bgr24_at(data, off_bits, width, height, top_down, row_bytes, sx, sy, &bb, &gg, &rr);
            }
            u16 c = (u16)RGB8_to_565((int)rr, (int)gg, (int)bb);
            /* Same layout as consoleDrawChar: index = x*240 + (239 - y) */
            fb[dx * (u32)GSP_SCREEN_WIDTH + (239u - dy)] = c;
        }
    }

    /* CPU wrote linear FB; ensure GSP reads our pixels (see gfxFlushBuffers). */
    GSPGPU_FlushDataCache(
        fb,
        (u32)(GSP_SCREEN_WIDTH * GSP_SCREEN_HEIGHT_TOP * sizeof(u16)));
}
