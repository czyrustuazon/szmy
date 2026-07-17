#include "botbuttons.h"
#include "uilayout.h"
#include "bmp_util.h"
#include "audio.h"
#include "musiclist.h"
#include "music_paths.h"
#include <3ds/gfx.h>
#include <3ds.h>
#include <string.h>
#include "bottom_screen_bg_embed_bmp.h"

#define FB_PX(fb, x, y) ((fb)[(u32)(x) * 240u + (239u - (u32)(y))])

typedef struct {
    ui_rect_t      rect;
    bot_widget_id_t id;
} bot_slot_t;

static bot_slot_t s_slots[BOT_WID_COUNT];
static int        s_press_id = -1;
static int        s_last_touch_held;
static int        s_last_x, s_last_y;
static int        s_dirty = 1;
static int        s_prev_playing = -1;
static int        s_prev_paused  = -1;
static float      s_seek_progress;
static int        s_scrubbing;
static int        s_confirm_delete;
static char       s_delete_path[MUSIC_PATH_MAX];
static char       s_toast[28];
static int        s_toast_left; /* frames remaining; 0 = hidden */
static u16        s_bg_cache[BOT_W * BOT_H];
static int        s_bg_cache_ready;

#define TOAST_HOLD_FRAMES  120 /* ~2s at 60fps */
#define TOAST_FONT_W         5
#define TOAST_FONT_H         7
#define TOAST_FONT_GAP       1
#define TOAST_SCALE          1
#define TOAST_PAD_X          8
#define TOAST_PAD_Y          4
#define TOAST_TOP_Y          6 /* top-center of bottom screen */

static u16 rgb565(u8 r, u8 g, u8 b)
{
    return (u16)RGB8_to_565(r, g, b);
}

static void blit_bg(u16 *fb)
{
    bmp_view_t b;
    u32        x, y;

    if (!s_bg_cache_ready) {
        if (bmp_open(&b, bottom_screen_bg_embed_bmp, bottom_screen_bg_embed_bmp_size) != 0) {
            u16 c = rgb565(24, 24, 28);
            for (x = 0; x < (u32)BOT_W; x++)
                for (y = 0; y < (u32)BOT_H; y++)
                    FB_PX(s_bg_cache, x, y) = c;
        } else {
            for (y = 0; y < (u32)BOT_H; y++) {
                for (x = 0; x < (u32)BOT_W; x++) {
                    u8 B, G, R, A;
                    u32 sx = x * b.w / (u32)BOT_W;
                    u32 sy = y * b.h / (u32)BOT_H;
                    if (sx >= b.w)
                        sx = b.w - 1u;
                    if (sy >= b.h)
                        sy = b.h - 1u;
                    bmp_bgra(&b, sx, sy, &B, &G, &R, &A);
                    FB_PX(s_bg_cache, x, y) = rgb565(R, G, B);
                }
            }
        }
        s_bg_cache_ready = 1;
    }

    memcpy(fb, s_bg_cache, sizeof(s_bg_cache));
}

static void tint_rect(u16 *fb, ui_rect_t r, u8 dark)
{
    int x, y;
    int x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;

    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > BOT_W)
        x1 = BOT_W;
    if (y1 > BOT_H)
        y1 = BOT_H;
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            u16 *p = &FB_PX(fb, (u32)x, (u32)y);
            *p = bmp_mix565(*p, 0, 0, 0, dark);
        }
    }
}

static void fill_rounded_rect(u16 *fb, ui_rect_t r, u16 color, int radius)
{
    int x, y;
    int x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    int rad = radius;

    if (rad < 0)
        rad = 0;
    if (rad * 2 > r.w)
        rad = r.w / 2;
    if (rad * 2 > r.h)
        rad = r.h / 2;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > BOT_W)
        x1 = BOT_W;
    if (y1 > BOT_H)
        y1 = BOT_H;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            int lx = x - r.x;
            int ly = y - r.y;
            int cut = 0;

            if (lx < rad && ly < rad) {
                int dx = rad - 1 - lx, dy = rad - 1 - ly;
                cut = (dx * dx + dy * dy) > (rad * rad);
            } else if (lx >= r.w - rad && ly < rad) {
                int dx = lx - (r.w - rad), dy = rad - 1 - ly;
                cut = (dx * dx + dy * dy) > (rad * rad);
            } else if (lx < rad && ly >= r.h - rad) {
                int dx = rad - 1 - lx, dy = ly - (r.h - rad);
                cut = (dx * dx + dy * dy) > (rad * rad);
            } else if (lx >= r.w - rad && ly >= r.h - rad) {
                int dx = lx - (r.w - rad), dy = ly - (r.h - rad);
                cut = (dx * dx + dy * dy) > (rad * rad);
            }
            if (!cut)
                FB_PX(fb, (u32)x, (u32)y) = color;
        }
    }
}

static void plot_px(u16 *fb, int x, int y, u16 color)
{
    if (x < 0 || y < 0 || x >= BOT_W || y >= BOT_H)
        return;
    FB_PX(fb, (u32)x, (u32)y) = color;
}

/* 5x7 uppercase A–Z + space/digits; each row is a 5-bit mask (MSB left). */
static const u8 TOAST_GLYPH[37][7] = {
    /* A */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* B */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    /* C */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    /* D */ {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    /* E */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    /* F */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    /* G */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},
    /* H */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* I */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    /* J */ {0x01,0x01,0x01,0x01,0x11,0x11,0x0E},
    /* K */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    /* L */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    /* M */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    /* N */ {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    /* O */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* P */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    /* Q */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    /* R */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    /* S */ {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    /* T */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    /* U */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* V */ {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    /* W */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    /* X */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    /* Y */ {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    /* Z */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    /* 0 */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    /* 1 */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    /* 2 */ {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    /* 3 */ {0x1F,0x01,0x02,0x06,0x01,0x11,0x0E},
    /* 4 */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    /* 5 */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    /* 6 */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    /* 7 */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    /* 8 */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    /* 9 */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    /*   */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

static int toast_glyph_index(char c)
{
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= '0' && c <= '9')
        return 26 + (c - '0');
    return 36; /* space */
}

static void draw_toast_char(u16 *fb, int x, int y, char c, u16 color)
{
    const u8 *g = TOAST_GLYPH[toast_glyph_index(c)];
    int       row, col, sx, sy;

    for (row = 0; row < TOAST_FONT_H; row++) {
        for (col = 0; col < TOAST_FONT_W; col++) {
            if (!(g[row] & (0x10 >> col)))
                continue;
            for (sy = 0; sy < TOAST_SCALE; sy++)
                for (sx = 0; sx < TOAST_SCALE; sx++)
                    plot_px(fb, x + col * TOAST_SCALE + sx, y + row * TOAST_SCALE + sy, color);
        }
    }
}

static void toast_show(const char *msg)
{
    if (msg == NULL || msg[0] == '\0')
        return;
    strncpy(s_toast, msg, sizeof(s_toast) - 1);
    s_toast[sizeof(s_toast) - 1] = '\0';
    s_toast_left = TOAST_HOLD_FRAMES;
    s_dirty      = 1;
}

static const char *widget_toast_label(int id)
{
    switch ((bot_widget_id_t)id) {
    case BOT_WID_REPEAT:    return "REPEAT";
    case BOT_WID_SHUFFLE:   return "SHUFFLE";
    case BOT_WID_DELETE:    return "DELETE";
    case BOT_WID_PREV:      return "PREV";
    case BOT_WID_REWIND:    return "REWIND";
    case BOT_WID_PLAYPAUSE: return audio_is_playing() ? "PAUSE" : "PLAY";
    case BOT_WID_FF:        return "FORWARD";
    case BOT_WID_NEXT:      return "NEXT";
    case BOT_WID_COUNT:     break;
    }
    return NULL;
}

static void draw_toast(u16 *fb)
{
    int  len, cw, ch, tw, th, x0, y0, x, y, i;
    u16  bg, edge, fg;

    if (s_toast_left <= 0 || s_toast[0] == '\0')
        return;

    len = (int)strlen(s_toast);
    cw  = TOAST_FONT_W * TOAST_SCALE;
    ch  = TOAST_FONT_H * TOAST_SCALE;
    tw  = len * (cw + TOAST_FONT_GAP * TOAST_SCALE) - TOAST_FONT_GAP * TOAST_SCALE
          + TOAST_PAD_X * 2;
    th  = ch + TOAST_PAD_Y * 2;
    if (tw < 40)
        tw = 40;
    x0 = (BOT_W - tw) / 2;
    y0 = TOAST_TOP_Y;

    bg   = rgb565(20, 22, 28);
    edge = rgb565(200, 205, 215);
    fg   = rgb565(255, 255, 255);

    for (y = y0; y < y0 + th; y++) {
        for (x = x0; x < x0 + tw; x++) {
            int edge_px = (x == x0 || y == y0 || x == x0 + tw - 1 || y == y0 + th - 1);
            plot_px(fb, x, y, edge_px ? edge : bg);
        }
    }

    x = x0 + TOAST_PAD_X;
    y = y0 + TOAST_PAD_Y;
    for (i = 0; s_toast[i] != '\0'; i++) {
        draw_toast_char(fb, x, y, s_toast[i], fg);
        x += cw + TOAST_FONT_GAP * TOAST_SCALE;
    }
}

static void fill_circle(u16 *fb, int cx, int cy, int rad, u16 color)
{
    int x, y;
    int r2 = rad * rad;

    for (y = -rad; y <= rad; y++) {
        for (x = -rad; x <= rad; x++) {
            if (x * x + y * y <= r2)
                plot_px(fb, cx + x, cy + y, color);
        }
    }
}

static void draw_seek_thumb(u16 *fb, ui_rect_t thumb)
{
    int cx = thumb.x + thumb.w / 2;
    int cy = thumb.y + thumb.h / 2;
    int r  = BOT_SEEK_THUMB_R;

    /* Circular brushed-metal knob matching bottom chrome. */
    fill_circle(fb, cx, cy, r, rgb565(28, 28, 30));
    fill_circle(fb, cx, cy, r - 1, rgb565(92, 94, 100));
    fill_circle(fb, cx - 1, cy - 1, r - 3, rgb565(150, 152, 158));
    fill_circle(fb, cx, cy, 2, rgb565(60, 62, 68));
}

static void draw_seek_overlays(u16 *fb)
{
    ui_rect_t track = bot_seek_track();
    ui_rect_t fill;
    ui_rect_t thumb;
    int       fill_w;

    /* Groove is empty in bottom-clean.png — paint progress + live thumb only. */
    fill_rounded_rect(fb, track, rgb565(8, 8, 10), 2);

    fill_w = (int)(s_seek_progress * (float)track.w);
    if (fill_w < 0)
        fill_w = 0;
    if (fill_w > track.w)
        fill_w = track.w;
    if (fill_w > 0) {
        fill   = track;
        fill.w = fill_w;
        fill_rounded_rect(fb, fill, rgb565(70, 160, 80), 2);
    }

    thumb = bot_seek_thumb(s_seek_progress);
    draw_seek_thumb(fb, thumb);
}

static void redraw(u16 *fb)
{
    blit_bg(fb);
    draw_seek_overlays(fb);

    /* Press feedback only — transport icons live in the chrome BMP. */
    if (s_press_id >= 0 && s_press_id < (int)BOT_WID_COUNT)
        tint_rect(fb, s_slots[s_press_id].rect, 110u);

    draw_toast(fb);
}

/* Paint both bottom framebuffers so a later swap never shows a stale buffer
 * missing the seek thumb or toast (dirty-only + double-buffer flicker). */
static void redraw_bottom_both(PrintConsole *bottom)
{
    u16 *fb = (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (fb == NULL)
        return;
    if (bottom)
        bottom->frameBuffer = fb;
    redraw(fb);

    gfxFlushBuffers();
    gfxScreenSwapBuffers(GFX_BOTTOM, false);

    fb = (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (fb == NULL)
        return;
    if (bottom)
        bottom->frameBuffer = fb;
    redraw(fb);
}

static int hit_widget(int px, int py)
{
    int i;
    for (i = 0; i < (int)BOT_WID_COUNT; i++) {
        if (s_slots[i].rect.w <= 0 || s_slots[i].rect.h <= 0)
            continue;
        if (ui_rect_contains(s_slots[i].rect, px, py, BMP_HIT_PAD_DEFAULT))
            return i;
    }
    return -1;
}

static float seek_progress_from_x(int px)
{
    ui_rect_t track = bot_seek_track();
    float     t;

    if (track.w <= 0)
        return 0.f;
    t = (float)(px - track.x) / (float)track.w;
    if (t < 0.f)
        t = 0.f;
    if (t > 1.f)
        t = 1.f;
    return t;
}

/* s_confirm_delete: 0=idle, 1=await A/B delete, 2=dismissible notice */
static void begin_delete_confirm(void)
{
    const char *path = audio_current_path();

    if (path == NULL) {
        s_delete_path[0] = '\0';
        s_confirm_delete = 2;
        musiclist_set_prompt("Nothing playing to delete.", "A or B to dismiss");
        return;
    }

    strncpy(s_delete_path, path, sizeof(s_delete_path) - 1);
    s_delete_path[sizeof(s_delete_path) - 1] = '\0';
    s_confirm_delete = 1;
    musiclist_set_prompt("Delete current track?", NULL);
}

int botbuttons_confirm_active(void)
{
    return s_confirm_delete != 0;
}

void botbuttons_confirm_accept(void)
{
    if (s_confirm_delete == 2) {
        musiclist_set_prompt(NULL, NULL);
        s_confirm_delete = 0;
        s_dirty          = 1;
        return;
    }
    if (s_confirm_delete != 1)
        return;

    audio_stop_wait();
    if (musiclist_delete_file(s_delete_path) != 0) {
        musiclist_set_prompt("Delete failed.", "A or B to dismiss");
        s_confirm_delete = 2;
    } else {
        musiclist_set_prompt(NULL, NULL);
        s_confirm_delete = 0;
    }
    s_delete_path[0] = '\0';
    s_dirty          = 1;
}

void botbuttons_confirm_cancel(void)
{
    musiclist_set_prompt(NULL, NULL);
    s_confirm_delete = 0;
    s_delete_path[0] = '\0';
    s_dirty          = 1;
}

static void activate_widget(int id)
{
    if (s_confirm_delete)
        return;

    /* Toast is shown on press; don't re-show on release (avoids a flash). */

    switch ((bot_widget_id_t)id) {
    case BOT_WID_PLAYPAUSE:
        if (audio_is_playing())
            audio_pause();
        else if (audio_is_paused())
            audio_resume();
        else {
            const char *path = musiclist_play_path();
            if (path != NULL)
                (void)audio_play_file_async(path);
        }
        break;
    case BOT_WID_DELETE:
        begin_delete_confirm();
        break;
    case BOT_WID_PREV: {
        const char *cur  = audio_current_path();
        const char *prev = musiclist_prev_file_before(
            cur ? cur : musiclist_play_path());
        if (prev != NULL)
            (void)audio_play_file_async(prev);
        break;
    }
    case BOT_WID_NEXT: {
        const char *cur  = audio_current_path();
        const char *next = musiclist_next_file_after(
            cur ? cur : musiclist_play_path());
        if (next != NULL)
            (void)audio_play_file_async(next);
        break;
    }
    case BOT_WID_REPEAT:
    case BOT_WID_SHUFFLE:
    case BOT_WID_REWIND:
    case BOT_WID_FF:
    case BOT_WID_COUNT:
        break;
    }
    s_dirty = 1;
}

void botbuttons_init(PrintConsole *bottom)
{
    ui_rect_t layout[BOT_WID_COUNT];
    int       i;

    (void)bottom;
    bot_layout_all(layout);

    for (i = 0; i < (int)BOT_WID_COUNT; i++) {
        s_slots[i].rect = layout[i];
        s_slots[i].id   = (bot_widget_id_t)i;
    }

    s_press_id        = -1;
    s_last_touch_held = 0;
    s_seek_progress   = 0.f;
    s_scrubbing       = 0;
    s_confirm_delete  = 0;
    s_delete_path[0]  = '\0';
    s_toast[0]        = '\0';
    s_toast_left      = 0;
    s_dirty           = 1;
    s_prev_playing    = -1;
    s_prev_paused     = -1;
}

void botbuttons_frame(PrintConsole *bottom)
{
    u32  held  = hidKeysHeld();
    int  tdown = (held & KEY_TOUCH) != 0;
    int  px, py;
    int  playing;
    int  paused;

    if (tdown) {
        touchPosition t;
        hidTouchRead(&t);
        px = t.px;
        py = t.py;
    } else {
        px = s_last_x;
        py = s_last_y;
    }

    /* Prefer buttons over seek — seek band can overlap transport Y. */
    if (s_confirm_delete) {
        s_press_id  = -1;
        s_scrubbing = 0;
    } else if (tdown) {
        int id = hit_widget(px, py);
        if (id >= 0) {
            if (id != s_press_id) {
                const char *label = widget_toast_label(id);
                if (label != NULL)
                    toast_show(label);
                s_dirty = 1;
            }
            s_press_id  = id;
            s_scrubbing = 0;
        } else if (s_scrubbing || ui_rect_contains(bot_band_seek(), px, py, 2)) {
            float p = seek_progress_from_x(px);
            if (!s_scrubbing || p != s_seek_progress)
                s_dirty = 1;
            s_seek_progress = p;
            s_scrubbing     = 1;
            s_press_id      = -1;
        } else {
            if (s_press_id >= 0)
                s_dirty = 1;
            s_press_id  = -1;
            s_scrubbing = 0;
        }
    } else {
        if (s_last_touch_held) {
            if (s_scrubbing) {
                (void)audio_seek_ratio(s_seek_progress);
                s_dirty = 1;
            } else if (s_press_id >= 0)
                activate_widget(s_press_id);
        }
        if (s_press_id >= 0 || s_scrubbing)
            s_dirty = 1;
        s_press_id  = -1;
        s_scrubbing = 0;
    }

    if (tdown) {
        s_last_x = px;
        s_last_y = py;
    }
    s_last_touch_held = tdown;

    playing = audio_is_playing();
    paused  = audio_is_paused();
    if (playing != s_prev_playing || paused != s_prev_paused) {
        s_dirty        = 1;
        s_prev_playing = playing;
        s_prev_paused  = paused;
    }

    if (!s_scrubbing && (playing || paused)) {
        float p = audio_progress_ratio();
        /* Dirty when thumb would move by ~1px on the 256px seek track. */
        if ((int)(p * (float)BOT_SEEK_TRACK_W) !=
            (int)(s_seek_progress * (float)BOT_SEEK_TRACK_W))
            s_dirty = 1;
        s_seek_progress = p;
    } else if (!s_scrubbing && !playing && !paused) {
        if (s_seek_progress != 0.f) {
            s_seek_progress = 0.f;
            s_dirty         = 1;
        }
    }

    if (s_toast_left > 0) {
        s_toast_left--;
        if (s_toast_left <= 0) {
            s_toast[0] = '\0';
            s_dirty    = 1; /* one clean redraw to clear toast */
        }
    }

    if (s_dirty) {
        redraw_bottom_both(bottom);
        s_dirty = 0;
    }
}

#undef FB_PX
