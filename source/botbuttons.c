#include "botbuttons.h"
#include "bmp_util.h"
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

#define FB_PX(fb, x, y) ((fb)[(u32)(x) * 240u + (239u - (u32)(y))])

static u16 bg_panel_color(void)
{
    return (u16)RGB8_to_565(32, 32, 40);
}

static void strip_fill(u16 *fb)
{
    u16     c  = bg_panel_color();
    u32     x, y;
    for (x = 0; x < 320u; x++) {
        for (y = BTN_ROW_Y0; y < 240u; y++)
            FB_PX(fb, x, y) = c;
    }
}

static void draw_bmp(u16 *fb, bmp_view_t *bmp, int x0, int y0)
{
    u32 x, y;
    u32 w = bmp->w, h = bmp->h;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            u8 B, G, R, A;
            bmp_bgra(bmp, x, y, &B, &G, &R, &A);
            if (bmp->bpp == 24u)
                A = bmp_alpha_key_24(B, G, R);
            if (A < 3u)
                continue;
            int px = (int)x0 + (int)x, py = (int)y0 + (int)y;
            if (px < 0 || py < (int)BTN_ROW_Y0 || px > 319 || py > 239)
                continue;
            u16 *d = &FB_PX(fb, (u32)px, (u32)py);
            *d     = bmp_mix565(*d, R, G, B, (u32)A);
        }
    }
}

static int bmp_draw_center(u16 *fb, const u8 *data, u32 dsize, int cx, int cy, int *out_w, int *out_h)
{
    bmp_view_t b;
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

static u32    s_paw, s_pah, s_piw, s_pih, s_saw, s_sah, s_siw, s_sih;
static int    s_pdx0, s_pdy0, s_sdx0, s_sdy0;
static int    s_play_down;
static int    s_pause_down;
static int    s_prev_play_down;
static int    s_prev_pause_down;
static int    s_last_touch_held;
static int    s_last_x, s_last_y;

void botbuttons_init(PrintConsole *bottom)
{
    bmp_view_t b;
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
        u32 pw = s_paw > s_piw ? s_paw : s_piw;
        u32 ph = s_pah > s_pih ? s_pah : s_pih;
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
        u32 sw = s_saw > s_siw ? s_saw : s_siw;
        u32 sh = s_sah > s_sih ? s_sah : s_sih;
        s_sdx0 = PAUSE_CX - (int)sw / 2;
        s_sdy0 = BTN_CY - (int)sh / 2;
    }
}

void botbuttons_frame(PrintConsole *bottom)
{
    u32     held  = hidKeysHeld();
    int     tdown = (held & KEY_TOUCH) != 0;
    int     px, py;
    u16    *s_fb  = (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (s_fb == NULL)
        return;
    if (bottom)
        bottom->frameBuffer = s_fb;

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
        if (bmp_point_in(
                s_last_x, s_last_y, s_pdx0, s_pdy0,
                (int)(s_paw > s_piw ? s_paw : s_piw), (int)(s_pah > s_pih ? s_pah : s_pih),
                BMP_HIT_PAD_DEFAULT)) {
            const char *path = musiclist_play_path();
            if (path != NULL)
                (void)audio_play_file_async(path);
        } else if (bmp_point_in(
                     s_last_x, s_last_y, s_sdx0, s_sdy0,
                     (int)(s_saw > s_siw ? s_saw : s_siw), (int)(s_sah > s_sih ? s_sah : s_sih),
                     BMP_HIT_PAD_DEFAULT)
                 && audio_is_playing()) {
            audio_pause();
        }
    }

    s_play_down = tdown && bmp_point_in(
        px, py, s_pdx0, s_pdy0,
        (int)(s_paw > s_piw ? s_paw : s_piw), (int)(s_pah > s_pih ? s_pah : s_pih),
        BMP_HIT_PAD_DEFAULT);
    s_pause_down = tdown && bmp_point_in(
        px, py, s_sdx0, s_sdy0,
        (int)(s_saw > s_siw ? s_saw : s_siw), (int)(s_sah > s_sih ? s_sah : s_sih),
        BMP_HIT_PAD_DEFAULT);
    if (tdown) {
        s_last_x = px;
        s_last_y = py;
    }
    s_last_touch_held = tdown;

    if (s_prev_play_down < 0 || s_play_down != s_prev_play_down || s_pause_down != s_prev_pause_down) {
        strip_fill(s_fb);
        {
            int dw, dh;
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
}

#undef FB_PX
