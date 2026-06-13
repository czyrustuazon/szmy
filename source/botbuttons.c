#include "botbuttons.h"
#include "audio.h"
#include "musiclist.h"
#include <3ds/gfx.h>
#include <3ds.h>
#include <string.h>
#include "play_active_bmp.h"
#include "play_inactive_bmp.h"
#include "pause_active_bmp.h"
#include "pause_inactive_bmp.h"

#define BTN_ROW_Y0 160
#define PLAY_CX    80
#define PAUSE_CX   240
#define BTN_CY     200
#define HIT_PAD    6

/* Bottom: 320x240, same linear layout as top: i = x*240 + (239-y) */
#define FB_PX(fb, x, y) ((fb)[(u32)(x) * 240u + (239u - (u32)(y))])

static u32 u32le(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static u32 u16le(const u8 *p) { return (u32)(p[0] | (p[1] << 8)); }

typedef struct {
    const u8 *data;
    u32            size;
    u32            w, h;
    u32            off;
    u32            row_b;
    u32            comp; /* DIB: BI_RGB=0, BI_BITFIELDS=3 */
    u16            bpp;
    int            top_down;
} Bmp;

static int bmp_open(Bmp *b, const u8 *data, u32 size) {
    if (size < 54u || data[0] != 'B' || data[1] != 'M')
        return -1;
    u32  width  = u32le(data + 18);
    s32  iheight  = (s32)u32le(data + 22);
    u32  height  = (u32)(iheight < 0 ? -iheight : iheight);
    u32  comp  = u32le(data + 30);
    u16  bpp  = (u16)u16le(data + 28);
    b->data    = data;
    b->size    = size;
    b->w       = width;
    b->h       = height;
    b->off     = u32le(data + 10);
    b->comp    = comp;
    b->bpp     = bpp;
    b->top_down = (iheight < 0) ? 1 : 0;
    b->row_b   = (bpp * width + 31u) / 32u * 4u;
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

static void bmp_bgra(Bmp *b, u32 x, u32 y, u8 *B, u8 *G, u8 *R, u8 *A) {
    u32   yf = b->top_down ? y : (b->h - 1u - y);
    u32   rb = b->row_b;
    const u8 *row = b->data + b->off + yf * rb;
    if (b->bpp == 32u) {
        const u8 *p = row + x * 4u;
        *B = p[0];
        *G = p[1];
        *R = p[2];
        *A = p[3];
        /* Many BMPs write A=0 for "unused" (BI_RGB) or forget alpha; if RGB is not black, use opaque. */
        if (b->comp == 0u) {
            if (*A < 2u) {
                u32  rgb = (u32)(*R) + (u32)(*G) + (u32)(*B);
                *A  = (rgb > 4u) ? 255u : 0u;
            }
        } else if (*A < 2u) {
            u32  rgb2 = (u32)(*R) + (u32)(*G) + (u32)(*B);
            if (rgb2 > 20u)
                *A = 255u;
        }
    } else {
        const u8 *p = row + x * 3u;
        *B = p[0];
        *G = p[1];
        *R = p[2];
        *A = 255u;
    }
}

static u16 mix565(u16 dst, u8 sr, u8 sg, u8 sb, u32 sa) {
    if (sa < 1u)
        return dst;
    u32 r5 = (u32)(dst >> 11) & 31u, g6 = (dst >> 5) & 63u, b5 = dst & 31u;
    u32 dr  = (r5 << 3) | (r5 >> 2);
    u32 dg  = (g6 << 2) | (g6 >> 4);
    u32 db  = (b5 << 3) | (b5 >> 2);
    u32 a   = sa;
    u32 inv = 255u - a;
    u32 oR  = (sr * a + dr * inv) / 255u;
    u32 oG  = (sg * a + dg * inv) / 255u;
    u32 oB  = (sb * a + db * inv) / 255u;
    u32 o5  = oR >> 3;
    u32 o6  = oG >> 2;
    u32 ob5 = oB >> 3;
    if (o5 > 31u)
        o5 = 31u;
    if (o6 > 63u)
        o6 = 63u;
    if (ob5 > 31u)
        ob5 = 31u;
    return (u16)(o5 | (o6 << 5) | (ob5 << 11));
}

/* 24-bit black keying (fallback when not BMP4) — keeps brighter glow pixels */
static u8 alpha_key(u8 b, u8 g, u8 r) {
    u32 t = (u32)b + (u32)g + (u32)r;
    u32 m = (b > g) ? (b > r ? b : r) : (g > r ? g : r);
    if (t < 8u)
        return 0;
    if (m < 18u && t < 30u) /* very dark, likely fringe / glow: partial alpha */
        return (u8)(t * 8u);
    if (m < 40u) /* near-black: semi transparent */
        return (u8)(80u + m * 3u);
    return 255u;
}

static u16 bg_panel_color(void) {
    return (u16)RGB8_to_565(32, 32, 40);
}

static void strip_fill(u16 *fb) {
    u16     c  = bg_panel_color();
    u32     x, y;
    for (x = 0; x < 320u; x++) {
        for (y = BTN_ROW_Y0; y < 240u; y++)
            FB_PX(fb, x, y) = c;
    }
}

static void draw_bmp(
    u16 *fb, Bmp *bmp, int x0, int y0) {
    u32   x, y;
    u32   w = bmp->w, h = bmp->h;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            u8 B, G, R, A;
            bmp_bgra(bmp, x, y, &B, &G, &R, &A);
            if (bmp->bpp == 24u)
                A = alpha_key(B, G, R);
            if (A < 3u)
                continue;
            int   px = (int)x0 + (int)x, py = (int)y0 + (int)y;
            if (px < 0 || py < (int)BTN_ROW_Y0 || px > 319 || py > 239)
                continue;
            u16 *d = &FB_PX(fb, (u32)px, (u32)py);
            *d     = mix565(*d, R, G, B, (u32)A);
        }
    }
}

static int bmp_draw_center(u16 *fb, const u8 *data, u32 dsize, int cx, int cy, int *out_w, int *out_h) {
    Bmp b;
    if (bmp_open(&b, data, dsize) != 0)
        return -1;
    *out_w = (int)b.w;
    *out_h = (int)b.h;
    {
        int x0 = cx - (int)b.w / 2;
        int y0 = cy - (int)b.h / 2;
        if (x0 < 0)
            x0 = 0;
        if (y0 < (int)BTN_ROW_Y0)
            y0 = (int)BTN_ROW_Y0;
        if (x0 + (int)b.w > 320)
            x0 = 320 - (int)b.w;
        if (y0 + (int)b.h > 240)
            y0 = 240 - (int)b.h;
        draw_bmp(fb, &b, x0, y0);
    }
    return 0;
}

static int point_in(int px, int py, int x0, int y0, int w, int h) {
    if (w <= 0 || h <= 0)
        return 0;
    return (px + HIT_PAD >= x0
            && px - HIT_PAD < x0 + w
            && py + HIT_PAD >= y0
            && py - HIT_PAD < y0 + h);
}

static u32    s_paw, s_pah, s_piw, s_pih, s_saw, s_sah, s_siw, s_sih;
static int    s_pdx0, s_pdy0, s_sdx0, s_sdy0;
static int    s_play_down;
static int    s_pause_down;
static int    s_prev_play_down;
static int    s_prev_pause_down;
static int    s_last_touch_held;
static int    s_last_x, s_last_y;

void botbuttons_init(PrintConsole *bottom) {
    Bmp  b;
    (void)bottom;
    s_prev_play_down  = -1;
    s_prev_pause_down = -1;
    s_paw = s_pah = s_piw = s_pih = 32u;
    s_saw = s_sah = s_siw = s_sih = 32u;
    s_pdx0  = PLAY_CX - 16;
    s_pdy0  = BTN_CY - 16;
    s_sdx0  = PAUSE_CX - 16;
    s_sdy0  = BTN_CY - 16;
    if (bmp_open(&b, play_active_bmp, play_active_bmp_size) == 0) {
        s_paw = b.w;
        s_pah = b.h;
    }
    if (bmp_open(&b, play_inactive_bmp, play_inactive_bmp_size) == 0) {
        s_piw = b.w;
        s_pih = b.h;
    }
    {
        u32  pw  = s_paw > s_piw ? s_paw : s_piw;
        u32  ph  = s_pah > s_pih ? s_pah : s_pih;
        s_pdx0 = PLAY_CX - (int)pw / 2;
        s_pdy0 = BTN_CY - (int)ph / 2;
    }
    if (bmp_open(&b, pause_active_bmp, pause_active_bmp_size) == 0) {
        s_saw = b.w;
        s_sah = b.h;
    }
    if (bmp_open(&b, pause_inactive_bmp, pause_inactive_bmp_size) == 0) {
        s_siw = b.w;
        s_sih = b.h;
    }
    {
        u32  sw  = s_saw > s_siw ? s_saw : s_siw;
        u32  sh  = s_sah > s_sih ? s_sah : s_sih;
        s_sdx0 = PAUSE_CX - (int)sw / 2;
        s_sdy0 = BTN_CY - (int)sh / 2;
    }
}

void botbuttons_frame(PrintConsole *bottom) {
    u32     held  = hidKeysHeld();
    int     tdown = (held & KEY_TOUCH) != 0;
    int     px, py;
    u16  *s_fb    = (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (s_fb == NULL)
        return;
    if (bottom)
        bottom->frameBuffer = s_fb; /* same buffer as libctru each frame; avoids stale/double buffer mismatch */

    if (tdown) {
        touchPosition t;
        hidTouchRead(&t);
        px = t.px;
        py = t.py;
    } else {
        px = s_last_x;
        py = s_last_y;
    }

    if (s_last_touch_held && !tdown) {
        if (point_in(
                s_last_x, s_last_y, s_pdx0, s_pdy0, (int)(s_paw > s_piw ? s_paw : s_piw), (int)(s_pah > s_pih ? s_pah : s_pih))) {
            const char *path = musiclist_play_path();
            if (path != NULL)
                (void)audio_play_file_async(path);
        } else if (point_in(
                     s_last_x, s_last_y, s_sdx0, s_sdy0, (int)(s_saw > s_siw ? s_saw : s_siw), (int)(s_sah > s_sih ? s_sah : s_sih))
                 && audio_is_playing()) {
            audio_pause();
        }
    }

    s_play_down
        = tdown
        && point_in(
               px, py, s_pdx0, s_pdy0, (int)(s_paw > s_piw ? s_paw : s_piw), (int)(s_pah > s_pih ? s_pah : s_pih));
    s_pause_down
        = tdown
        && point_in(
               px, py, s_sdx0, s_sdy0, (int)(s_saw > s_siw ? s_saw : s_siw), (int)(s_sah > s_sih ? s_sah : s_sih));
    if (tdown) {
        s_last_x = px;
        s_last_y = py;
    }
    s_last_touch_held = tdown;

    /* Redraw only when button visuals change — full alpha blit every frame starved audio. */
    if (s_prev_play_down < 0 || s_play_down != s_prev_play_down || s_pause_down != s_prev_pause_down) {
        strip_fill(s_fb);
        {
            int  dw, dh;
            (void)bmp_draw_center(
                s_fb, s_play_down ? play_active_bmp : play_inactive_bmp,
                s_play_down ? play_active_bmp_size : play_inactive_bmp_size, PLAY_CX, BTN_CY, &dw, &dh);
            (void)bmp_draw_center(
                s_fb, s_pause_down ? pause_active_bmp : pause_inactive_bmp,
                s_pause_down ? pause_active_bmp_size : pause_inactive_bmp_size, PAUSE_CX, BTN_CY, &dw, &dh);
        }
        s_prev_play_down = s_play_down;
        s_prev_pause_down = s_pause_down;
    }
    /* D-cache: main calls gfxFlushBuffers() right after, which flushes the same bottom FB */
}

#undef FB_PX
