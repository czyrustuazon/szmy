#include "botbuttons.h"
#include "uilayout.h"
#include "bmp_util.h"
#include "audio.h"
#include "audio_ctrl.h"
#include "audio_viz.h"
#include "ftp_server.h"
#include "musiclist.h"
#include "music_paths.h"
#include "softfont.h"
#include "trackmeta.h"
#include <3ds/gfx.h>
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bottom_screen_bg_embed_bmp.h"
#include "generic_music_embed_bmp.h"

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
static int        s_delete_is_dir;
static char       s_toast[28];
static int        s_toast_left; /* frames remaining; 0 = hidden */
static int        s_time_valid;
static int        s_time_pos;   /* displayed elapsed seconds */
static int        s_time_dur;   /* displayed total seconds */
static int        s_eq_show[AUDIO_VIZ_BARS]; /* eased levels, 0..255 */
static int        s_eq_h[AUDIO_VIZ_BARS];    /* drawn bar heights (px) */
static int        s_eq_live;                 /* draw EQ while any is up */
static int        s_eq_mode;                 /* 0 = bars, 1 = waveform */
static int        s_eq_press;                /* touch began on the EQ field */
static int        s_display_press;           /* touch began on large display */
static int8_t     s_eq_scope[AUDIO_VIZ_SCOPE]; /* drawn waveform window */
static float      s_eq_scope_pos;              /* smooth scroll cursor (points) */
static int        s_repeat_mode;               /* 0=off, 1=one track, 2=all */
static int        s_shuffle_on;                /* random no-repeat cycle */
static int        s_shuffle_pending;           /* bag build in progress */
static int        s_shuffle_play_when_ready;   /* start next track when bag ready */
static u16        s_bg_cache[BOT_W * BOT_H];
static int        s_bg_cache_ready;
static u16        s_shadow[BOT_W * BOT_H]; /* off-screen compose target */

/* Now-playing metadata shown on the display panel. */
#define META_ART_W 96
#define META_ART_H 96
#define META_TITLE_SCALE 0.62f
#define META_TITLE_GAP   36.0f
#define META_SCROLL_STEP 0.5f  /* pixels/frame: ~30 px/s at 60 Hz */
#define META_SCROLL_WAIT 60    /* hold the beginning for one second */
static trackmeta_t s_meta;
static char        s_meta_path[MUSIC_PATH_MAX];
static u16         s_art[META_ART_W * META_ART_H];       /* embedded cover */
static int         s_art_ready;
static u16         s_generic[META_ART_W * META_ART_H];   /* fallback icon */
static int         s_generic_ready;
static float       s_title_scroll;
static int         s_title_scroll_wait;

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

/* 5x7 uppercase A–Z + digits/space/colon/slash + punctuation for the
 * now-playing panel; each row is a 5-bit mask (MSB left). */
static const u8 TOAST_GLYPH[51][7] = {
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
    /* : */ {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
    /* / */ {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
    /* . */ {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    /* - */ {0x00,0x00,0x00,0x0E,0x00,0x00,0x00},
    /* _ */ {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},
    /* ( */ {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    /* ) */ {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    /* ' */ {0x04,0x04,0x08,0x00,0x00,0x00,0x00},
    /* & */ {0x08,0x14,0x14,0x08,0x15,0x12,0x0D},
    /* ! */ {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    /* ? */ {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
    /* , */ {0x00,0x00,0x00,0x00,0x0C,0x04,0x08},
    /* + */ {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    /* # */ {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A},
};

static int toast_glyph_index(char c)
{
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= '0' && c <= '9')
        return 26 + (c - '0');
    switch (c) {
    case ':':  return 37;
    case '/':  return 38;
    case '.':  return 39;
    case '-':  return 40;
    case '_':  return 41;
    case '(':  return 42;
    case ')':  return 43;
    case '\'': return 44;
    case '&':  return 45;
    case '!':  return 46;
    case '?':  return 47;
    case ',':  return 48;
    case '+':  return 49;
    case '#':  return 50;
    default:   return 36; /* space */
    }
}

static void draw_char_scaled(u16 *fb, int x, int y, char c, u16 color, int scale)
{
    const u8 *g = TOAST_GLYPH[toast_glyph_index(c)];
    int       row, col, sx, sy;

    for (row = 0; row < TOAST_FONT_H; row++) {
        for (col = 0; col < TOAST_FONT_W; col++) {
            if (!(g[row] & (0x10 >> col)))
                continue;
            for (sy = 0; sy < scale; sy++)
                for (sx = 0; sx < scale; sx++)
                    plot_px(fb, x + col * scale + sx, y + row * scale + sy, color);
        }
    }
}

static void draw_toast_char(u16 *fb, int x, int y, char c, u16 color)
{
    draw_char_scaled(fb, x, y, c, color, TOAST_SCALE);
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

void botbuttons_notify(const char *message)
{
    toast_show(message);
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
    case BOT_WID_FOLDER:    return "FTP";
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

/* Persistent mode badges (shuffle / repeat), top-left. Same boxed style as
 * the toast but with tighter padding, and they stay up while the mode is on. */
#define BADGE_X0     4
#define BADGE_Y0     4
#define BADGE_PAD_X  4
#define BADGE_PAD_Y  2
#define BADGE_GAP    3

static int draw_badge(u16 *fb, int x0, const char *text)
{
    int len = (int)strlen(text);
    int cw  = TOAST_FONT_W;
    int tw  = len * (cw + TOAST_FONT_GAP) - TOAST_FONT_GAP + BADGE_PAD_X * 2;
    int th  = TOAST_FONT_H + BADGE_PAD_Y * 2;
    int x, y, i;
    u16 bg   = rgb565(20, 22, 28);
    u16 edge = rgb565(200, 205, 215);
    u16 fg   = rgb565(255, 255, 255);

    for (y = BADGE_Y0; y < BADGE_Y0 + th; y++) {
        for (x = x0; x < x0 + tw; x++) {
            int edge_px = (x == x0 || y == BADGE_Y0
                           || x == x0 + tw - 1 || y == BADGE_Y0 + th - 1);
            plot_px(fb, x, y, edge_px ? edge : bg);
        }
    }

    x = x0 + BADGE_PAD_X;
    y = BADGE_Y0 + BADGE_PAD_Y;
    for (i = 0; text[i] != '\0'; i++) {
        draw_toast_char(fb, x, y, text[i], fg);
        x += cw + TOAST_FONT_GAP;
    }
    return x0 + tw + BADGE_GAP;
}

static void draw_status_badges(u16 *fb)
{
    int x = BADGE_X0;

    if (s_shuffle_on)
        x = draw_badge(fb, x, "SHUFFLE");
    if (s_repeat_mode == 1)
        x = draw_badge(fb, x, "REPEAT 1");
    else if (s_repeat_mode == 2)
        x = draw_badge(fb, x, "REPEAT ALL");
    (void)x;
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

/* Equalizer bars in the dark inset field between folder and trash. */
static void draw_eq_bars(u16 *fb)
{
    ui_rect_t r = bot_eq_rect();
    int pitch   = r.w / AUDIO_VIZ_BARS;
    int bw      = pitch - 4;
    int i;

    if (!s_eq_live)
        return;

    for (i = 0; i < AUDIO_VIZ_BARS; i++) {
        ui_rect_t bar;
        int h = s_eq_h[i];

        if (h <= 0)
            continue;
        bar = ui_rect(r.x + i * pitch + 2, r.y + r.h - h, bw, h);
        fill_rounded_rect(fb, bar, rgb565(70, 160, 80), 1);
        bar.h = 1; /* lighter cap on top of each bar */
        fill_rounded_rect(fb, bar, rgb565(150, 225, 160), 0);
    }
}

/* Waveform ("squiggly line") alternative: one scope point per pixel column,
 * with vertical segments joining neighbors so the trace stays connected. */
static void draw_eq_scope(u16 *fb)
{
    ui_rect_t r   = bot_eq_rect();
    int       mid = r.y + r.h / 2;
    int       prev_y = mid;
    int       i;

    if (!s_eq_live)
        return;

    for (i = 0; i < AUDIO_VIZ_SCOPE && i < r.w; i++) {
        int y  = mid - ((int)s_eq_scope[i] * (r.h / 2)) / 127;
        int y0 = (y < prev_y) ? y : prev_y;
        int y1 = (y < prev_y) ? prev_y : y;
        int yy;

        for (yy = y0; yy <= y1; yy++)
            plot_px(fb, r.x + i, yy, rgb565(70, 160, 80));
        plot_px(fb, r.x + i, y, rgb565(150, 225, 160)); /* bright core */
        prev_y = y;
    }
}

/* "M:SS / M:SS" right-aligned under the seek track; shadow keeps it
 * readable on the perforated-metal chrome. */
static void draw_seek_times(u16 *fb)
{
    char pos_buf[12], dur_buf[12], line[28];
    ui_rect_t track = bot_seek_track();
    int  len, w, x, y, i;

    if (!s_time_valid)
        return;

    audio_format_mmss(s_time_pos, pos_buf, sizeof(pos_buf));
    audio_format_mmss(s_time_dur, dur_buf, sizeof(dur_buf));
    snprintf(line, sizeof(line), "%s / %s", pos_buf, dur_buf);

    len = (int)strlen(line);
    w   = len * (TOAST_FONT_W + TOAST_FONT_GAP) - TOAST_FONT_GAP;
    x   = track.x + track.w - w;
    y   = track.y + track.h + 6;

    for (i = 0; line[i] != '\0'; i++) {
        draw_toast_char(fb, x + 1, y + 1, line[i], rgb565(10, 10, 12));
        draw_toast_char(fb, x, y, line[i], rgb565(210, 214, 222));
        x += TOAST_FONT_W + TOAST_FONT_GAP;
    }
}

/* --- Now-playing panel ------------------------------------------------------
 * Cover art (embedded picture, else the generic icon) on the left of the
 * grey display panel; title, filename and remaining tags on the right. */

static void ensure_generic_icon(void)
{
    bmp_view_t b;
    u32        x, y;

    if (s_generic_ready)
        return;
    s_generic_ready = 1;

    if (bmp_open(&b, generic_music_embed_bmp, generic_music_embed_bmp_size) != 0) {
        u16 c = rgb565(70, 74, 82);
        for (y = 0; y < (u32)(META_ART_W * META_ART_H); y++)
            s_generic[y] = c;
        return;
    }
    for (y = 0; y < (u32)META_ART_H; y++) {
        for (x = 0; x < (u32)META_ART_W; x++) {
            u8  B, G, R, A;
            u32 sx = x * b.w / (u32)META_ART_W;
            u32 sy = y * b.h / (u32)META_ART_H;
            if (sx >= b.w)
                sx = b.w - 1u;
            if (sy >= b.h)
                sy = b.h - 1u;
            bmp_bgra(&b, sx, sy, &B, &G, &R, &A);
            s_generic[y * META_ART_W + x] = rgb565(R, G, B);
        }
    }
}

/* Reload tags/art when the playing (or paused) track changes. */
static void meta_update(void)
{
    const char *cur = audio_current_path();

    if (cur == NULL) {
        if (s_meta_path[0] != '\0') {
            s_meta_path[0] = '\0';
            trackmeta_free(&s_meta);
            memset(&s_meta, 0, sizeof(s_meta));
            s_art_ready         = 0;
            s_title_scroll      = 0.f;
            s_title_scroll_wait = META_SCROLL_WAIT;
            s_dirty             = 1;
        }
        return;
    }
    if (strcmp(cur, s_meta_path) == 0)
        return;

    strncpy(s_meta_path, cur, sizeof(s_meta_path) - 1);
    s_meta_path[sizeof(s_meta_path) - 1] = '\0';
    trackmeta_free(&s_meta);
    (void)trackmeta_read(s_meta_path, &s_meta);
    s_art_ready = 0;
    if (s_meta.art != NULL) {
        if (trackmeta_decode_art(s_meta.art, s_meta.art_size,
                                 s_art, META_ART_W, META_ART_H) == 0)
            s_art_ready = 1;
        trackmeta_free(&s_meta); /* raw image bytes no longer needed */
    }
    s_title_scroll      = 0.f;
    s_title_scroll_wait = META_SCROLL_WAIT;
    s_dirty = 1;
}

static const char *meta_title_text(void)
{
    const char *fname;

    if (s_meta.title[0] != '\0')
        return s_meta.title;
    fname = strrchr(s_meta_path, '/');
    return fname != NULL ? fname + 1 : s_meta_path;
}

static void update_title_scroll(void)
{
    const float view_w = (float)(BOT_DISPLAY_X + BOT_DISPLAY_W - 8
                       - (BOT_DISPLAY_X + 9 + META_ART_W + 10));
    float width;
    float period;

    if (s_meta_path[0] == '\0' || !softfont_ok())
        return;
    width = softfont_width(META_TITLE_SCALE, meta_title_text());
    if (width <= view_w) {
        s_title_scroll = 0.f;
        return;
    }
    if (s_title_scroll_wait > 0) {
        s_title_scroll_wait--;
        return;
    }

    period = width + META_TITLE_GAP;
    s_title_scroll += META_SCROLL_STEP;
    while (s_title_scroll >= period)
        s_title_scroll -= period;
    s_dirty = 1;
}

/* Draw UTF-8 text clipped to max_w; codepoints outside the 5x7 font show
 * as '?' so CJK titles still hint that text is present. */
static void draw_meta_text(u16 *fb, int x, int y, int max_w, int scale,
                           const char *utf8, u16 color)
{
    int adv = (TOAST_FONT_W + TOAST_FONT_GAP) * scale;
    int x1  = x + max_w;

    while (*utf8 != '\0') {
        unsigned char c = (unsigned char)*utf8;
        char          ch;

        if (c < 0x80) {
            ch = (char)c;
            utf8++;
        } else {
            ch = '?';
            utf8++;
            while ((*utf8 & 0xC0) == 0x80)
                utf8++;
        }
        if (x + TOAST_FONT_W * scale > x1)
            break;
        draw_char_scaled(fb, x, y, ch, color, scale);
        x += adv;
    }
}

static void draw_display_meta(u16 *fb)
{
    const u16  *art;
    const char *fname;
    char        extra[64];
    int         ax, ay, tx, ty, tw, x, y;
    const int   line_h  = TOAST_FONT_H + 4;
    const int   bottom  = BOT_DISPLAY_Y + BOT_DISPLAY_H - 8;
    u16         c_title = rgb565(16, 18, 24);
    u16         c_file  = rgb565(52, 56, 66);
    u16         c_tags  = rgb565(38, 42, 52);

    if (s_meta_path[0] == '\0')
        return;

    ensure_generic_icon();
    art = s_art_ready ? s_art : s_generic;

    ax = BOT_DISPLAY_X + 9;
    ay = BOT_DISPLAY_Y + (BOT_DISPLAY_H - META_ART_H) / 2;
    for (y = 0; y < META_ART_H; y++)
        for (x = 0; x < META_ART_W; x++)
            plot_px(fb, ax + x, ay + y, art[y * META_ART_W + x]);
    /* thin frame so light covers don't bleed into the panel */
    for (x = -1; x <= META_ART_W; x++) {
        plot_px(fb, ax + x, ay - 1, rgb565(40, 42, 48));
        plot_px(fb, ax + x, ay + META_ART_H, rgb565(40, 42, 48));
    }
    for (y = -1; y <= META_ART_H; y++) {
        plot_px(fb, ax - 1, ay + y, rgb565(40, 42, 48));
        plot_px(fb, ax + META_ART_W, ay + y, rgb565(40, 42, 48));
    }

    fname = strrchr(s_meta_path, '/');
    fname = (fname != NULL) ? fname + 1 : s_meta_path;

    tx = ax + META_ART_W + 10;
    tw = BOT_DISPLAY_X + BOT_DISPLAY_W - 8 - tx;
    ty = ay;

    extra[0] = '\0';
    if (s_meta.year[0] != '\0')
        snprintf(extra, sizeof(extra), "%s", s_meta.year);
    if (s_meta.genre[0] != '\0')
        snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra),
                 "%s%s", extra[0] ? "  " : "", s_meta.genre);
    if (s_meta.track[0] != '\0')
        snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra),
                 "%s#%s", extra[0] ? "  " : "", s_meta.track);

    if (softfont_ok()) {
        /* System font (same BCFNT as the top screen), CPU-rasterized.
         * Free-form scales: ~19 px title, ~14 px detail lines. */
        const float big = META_TITLE_SCALE, small = 0.45f;
        const char *title = meta_title_text();
        float fx   = (float)tx;
        float fx1  = (float)(tx + tw);
        float fy   = (float)ty;
        float bh   = softfont_line_height(big);
        float sh   = softfont_line_height(small);
        float fbot = (float)(bottom + TOAST_FONT_H); /* panel text limit */
        float title_w = softfont_width(big, title);

        if (title_w > (float)tw) {
            float first_x = fx - s_title_scroll;
            float period  = title_w + META_TITLE_GAP;

            (void)softfont_draw_clipped(
                fb, first_x, fy, big, c_title, title, fx, fx1);
            (void)softfont_draw_clipped(
                fb, first_x + period, fy, big, c_title, title, fx, fx1);
        } else {
            (void)softfont_draw(fb, fx, fy, big, c_title, title, fx1);
        }
        fy += bh + 1.f;
        if (s_meta.title[0] != '\0' && fy + sh <= fbot) {
            (void)softfont_draw(fb, fx, fy, small, c_file, fname, fx1);
            fy += sh;
        }
        if (s_meta.artist[0] != '\0' && fy + sh <= fbot) {
            (void)softfont_draw(fb, fx, fy, small, c_tags, s_meta.artist, fx1);
            fy += sh;
        }
        if (s_meta.album[0] != '\0' && fy + sh <= fbot) {
            (void)softfont_draw(fb, fx, fy, small, c_tags, s_meta.album, fx1);
            fy += sh;
        }
        if (extra[0] != '\0' && fy + sh <= fbot)
            (void)softfont_draw(fb, fx, fy, small, c_tags, extra, fx1);
        return;
    }

    /* Fallback: built-in 5x7 font (system font not mapped). */
    draw_meta_text(fb, tx, ty, tw, 2,
                   s_meta.title[0] != '\0' ? s_meta.title : fname, c_title);
    ty += TOAST_FONT_H * 2 + 5;

    if (s_meta.title[0] != '\0' && ty + TOAST_FONT_H <= bottom) {
        draw_meta_text(fb, tx, ty, tw, 1, fname, c_file);
        ty += line_h;
    }
    if (s_meta.artist[0] != '\0' && ty + TOAST_FONT_H <= bottom) {
        draw_meta_text(fb, tx, ty, tw, 1, s_meta.artist, c_tags);
        ty += line_h;
    }
    if (s_meta.album[0] != '\0' && ty + TOAST_FONT_H <= bottom) {
        draw_meta_text(fb, tx, ty, tw, 1, s_meta.album, c_tags);
        ty += line_h;
    }
    if (extra[0] != '\0' && ty + TOAST_FONT_H <= bottom)
        draw_meta_text(fb, tx, ty, tw, 1, extra, c_tags);
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

    draw_seek_times(fb);
}

static void redraw(u16 *fb)
{
    blit_bg(fb);
    draw_display_meta(fb);
    if (s_eq_mode)
        draw_eq_scope(fb);
    else
        draw_eq_bars(fb);
    draw_seek_overlays(fb);
    draw_status_badges(fb);
    draw_toast(fb);
}

/* Compose the full frame off-screen, then copy the finished image to the
 * single-buffered bottom framebuffer in one pass. The display never sees a
 * half-composed frame (bg without overlays), so nothing can flash. */
static void present_bottom(PrintConsole *bottom)
{
    u16 *fb = (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (fb == NULL)
        return;
    if (bottom)
        bottom->frameBuffer = fb;
    redraw(s_shadow);
    memcpy(fb, s_shadow, sizeof(s_shadow));
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
static int path_is_entry_or_child(const char *entry, const char *path)
{
    size_t n;

    if (entry == NULL || path == NULL)
        return 0;
    n = strlen(entry);
    return strncmp(entry, path, n) == 0
           && (path[n] == '\0' || path[n] == '/');
}

static void begin_delete_confirm(void)
{
    const char *path = musiclist_selected_path();

    if (path == NULL) {
        s_delete_path[0] = '\0';
        s_delete_is_dir  = 0;
        s_confirm_delete = 2;
        musiclist_set_prompt("Nothing selected to delete.", "A or B to dismiss");
        return;
    }

    strncpy(s_delete_path, path, sizeof(s_delete_path) - 1);
    s_delete_path[sizeof(s_delete_path) - 1] = '\0';
    s_delete_is_dir = musiclist_selected_is_dir();
    s_confirm_delete = 1;
    musiclist_set_prompt(s_delete_is_dir
                             ? "Delete selected folder and its contents?"
                             : "Delete selected track?",
                         NULL);
}

int botbuttons_confirm_active(void)
{
    return s_confirm_delete != 0;
}

int botbuttons_delete_now(const char *path, int is_dir)
{
    char victim[MUSIC_PATH_MAX];
    char next_path[MUSIC_PATH_MAX];
    int  play_next        = 0;
    int  deleting_playing = 0;

    if (path == NULL || path[0] == '\0')
        return -1;

    /* audio_stop_wait() clears audio_current_path(); copy first. */
    strncpy(victim, path, sizeof(victim) - 1);
    victim[sizeof(victim) - 1] = '\0';
    next_path[0] = '\0';

    /* Keep unrelated playback running. Stop only when deleting the playing
     * file itself or a folder that contains it. If it is the playing file,
     * resolve and copy its successor before deletion refreshes the list. */
    if (!is_dir && audio_current_path() != NULL
        && strcmp(victim, audio_current_path()) == 0) {
        const char *next = musiclist_next_file_after(victim);

        deleting_playing = 1;
        if (next != NULL) {
            strncpy(next_path, next, sizeof(next_path) - 1);
            next_path[sizeof(next_path) - 1] = '\0';
            play_next = 1;
        }
        audio_stop_wait();
    } else if (path_is_entry_or_child(victim, audio_current_path())) {
        audio_stop_wait();
    }

    if (musiclist_delete_entry(victim, is_dir) != 0)
        return -1;

    /* Repeat-all wraps when the deleted playing track was the final one.
     * Repeat-one cannot replay a file that no longer exists. */
    if (deleting_playing && !play_next
        && botbuttons_repeat_mode() == 2) {
        const char *first = musiclist_first_file();
        if (first != NULL) {
            strncpy(next_path, first, sizeof(next_path) - 1);
            next_path[sizeof(next_path) - 1] = '\0';
            play_next = 1;
        }
    }
    if (play_next)
        (void)audio_play_file_async(next_path);
    s_dirty = 1;
    return 0;
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

    if (botbuttons_delete_now(s_delete_path, s_delete_is_dir) != 0) {
        musiclist_set_prompt("Delete failed.", "A or B to dismiss");
        s_confirm_delete = 2;
    } else {
        musiclist_set_prompt(NULL, NULL);
        s_confirm_delete = 0;
    }
    s_delete_path[0] = '\0';
    s_delete_is_dir  = 0;
    s_dirty          = 1;
}

void botbuttons_confirm_cancel(void)
{
    musiclist_set_prompt(NULL, NULL);
    s_confirm_delete = 0;
    s_delete_path[0] = '\0';
    s_delete_is_dir  = 0;
    s_dirty          = 1;
}

static void activate_widget(int id)
{
    if (s_confirm_delete)
        return;

    /* Toast is shown on press; don't re-show on release (avoids a flash). */

    switch ((bot_widget_id_t)id) {
    case BOT_WID_PLAYPAUSE:
        if (ftp_is_active()) {
            toast_show("FTP ACTIVE");
            break;
        }
        if (audio_is_playing())
            audio_pause();
        else if (audio_is_paused())
            audio_resume();
        else {
            const char *path = musiclist_play_path();
            if (path != NULL) {
                (void)audio_play_file_async(path);
                musiclist_shuffle_mark_played(path);
            }
        }
        break;
    case BOT_WID_DELETE:
        if (ftp_is_active()) {
            toast_show("FTP ACTIVE");
            break;
        }
        begin_delete_confirm();
        break;
    case BOT_WID_PREV: {
        const char *cur;
        const char *prev;
        if (ftp_is_active()) {
            toast_show("FTP ACTIVE");
            break;
        }
        cur  = audio_current_path();
        prev = musiclist_prev_file_before(
            cur ? cur : musiclist_play_path());
        if (prev != NULL)
            (void)audio_play_file_async(prev);
        break;
    }
    case BOT_WID_NEXT: {
        const char *next;
        if (ftp_is_active()) {
            toast_show("FTP ACTIVE");
            break;
        }
        if (s_shuffle_on) {
            next = musiclist_shuffle_next();
            if (next == NULL && s_repeat_mode == 2) {
                if (musiclist_shuffle_building()) {
                    botbuttons_shuffle_request_next();
                } else {
                    int n = musiclist_shuffle_start();
                    if (n > 0)
                        next = musiclist_shuffle_next();
                    else if (n < 0)
                        botbuttons_shuffle_request_next();
                }
            }
        } else {
            const char *cur = audio_current_path();
            next = musiclist_next_file_after(
                cur ? cur : musiclist_play_path());
        }
        if (next != NULL)
            (void)audio_play_file_async(next);
        break;
    }
    case BOT_WID_FOLDER: {
        int r = ftp_toggle();
        if (r > 0)
            toast_show("FTP ON");
        else if (r == 0) {
            (void)musiclist_refresh();
            toast_show("FTP OFF");
        } else
            toast_show("FTP FAIL");
        break;
    }
    case BOT_WID_REPEAT:
        s_repeat_mode = (s_repeat_mode + 1) % 3;
        /* Repeat-one conflicts with shuffle's no-repeat cycle. */
        if (s_shuffle_on && s_repeat_mode == 1)
            s_repeat_mode = 2;
        toast_show(s_repeat_mode == 1   ? "REPEAT ONE"
                   : s_repeat_mode == 2 ? "REPEAT ALL"
                                        : "REPEAT OFF");
        break;
    case BOT_WID_SHUFFLE:
        if (ftp_is_active()) {
            toast_show("FTP ACTIVE");
            break;
        }
        if (!s_shuffle_on) {
            int n;

            srand((unsigned)svcGetSystemTick());
            n = musiclist_shuffle_start();
            if (n > 0) {
                s_shuffle_on = 1;
                s_shuffle_pending = 0;
                /* The playing track already had its turn this cycle. */
                musiclist_shuffle_mark_played(audio_current_path());
                if (s_repeat_mode == 1)
                    s_repeat_mode = 2;
                toast_show("SHUFFLE ON");
            } else if (n < 0) {
                s_shuffle_on = 1;
                s_shuffle_pending = 1;
                s_shuffle_play_when_ready = 0;
                if (s_repeat_mode == 1)
                    s_repeat_mode = 2;
                toast_show("SHUFFLE…");
            } else {
                toast_show("NO TRACKS");
            }
        } else {
            s_shuffle_on = 0;
            s_shuffle_pending = 0;
            s_shuffle_play_when_ready = 0;
            musiclist_shuffle_stop();
            toast_show("SHUFFLE OFF");
        }
        break;
    case BOT_WID_REWIND:
    case BOT_WID_FF:
    case BOT_WID_COUNT:
        break;
    }
    s_dirty = 1;
}

int botbuttons_repeat_mode(void)
{
    return s_repeat_mode;
}

int botbuttons_shuffle_on(void)
{
    return s_shuffle_on;
}

void botbuttons_shuffle_request_next(void)
{
    s_shuffle_pending         = 1;
    s_shuffle_play_when_ready = 1;
    toast_show("SHUFFLE…");
    s_dirty = 1;
}

/* Finish an async shuffle bag build; play the next track if one was requested
 * while the bag was still being collected. Call once per main-loop tick. */
void botbuttons_shuffle_poll(void)
{
    int n;

    if (!s_shuffle_pending && !musiclist_shuffle_building())
        return;

    n = musiclist_shuffle_poll();
    if (n < 0)
        return;

    s_shuffle_pending = 0;
    if (n == 0) {
        s_shuffle_on = 0;
        s_shuffle_play_when_ready = 0;
        toast_show("NO TRACKS");
        s_dirty = 1;
        return;
    }

    if (!s_shuffle_play_when_ready) {
        /* Toggle-on path: current track already counts as played. */
        musiclist_shuffle_mark_played(audio_current_path());
        toast_show("SHUFFLE ON");
    } else {
        const char *next = musiclist_shuffle_next();

        s_shuffle_play_when_ready = 0;
        if (next != NULL) {
            (void)audio_play_file_async(next);
            (void)musiclist_select_path(next);
        }
        toast_show("SHUFFLE ON");
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
    s_delete_is_dir   = 0;
    s_toast[0]        = '\0';
    s_toast_left      = 0;
    s_time_valid      = 0;
    s_time_pos        = 0;
    s_time_dur        = 0;
    memset(s_eq_show, 0, sizeof(s_eq_show));
    memset(s_eq_h, 0, sizeof(s_eq_h));
    memset(s_eq_scope, 0, sizeof(s_eq_scope));
    s_eq_scope_pos    = 0.f;
    s_eq_live         = 0;
    s_eq_mode         = 0;
    audio_viz_set_scope_mode(0);
    s_eq_press        = 0;
    s_display_press   = 0;
    s_repeat_mode     = 0;
    s_shuffle_on      = 0;
    s_shuffle_pending = 0;
    s_shuffle_play_when_ready = 0;
    trackmeta_free(&s_meta);
    memset(&s_meta, 0, sizeof(s_meta));
    s_meta_path[0]    = '\0';
    s_art_ready       = 0;
    s_title_scroll    = 0.f;
    s_title_scroll_wait = META_SCROLL_WAIT;
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
        s_eq_press  = 0;
        s_display_press = 0;
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
            s_eq_press  = 0;
            s_display_press = 0;
        } else if (!ftp_is_active()
                   && (s_scrubbing
                       || ui_rect_contains(bot_band_seek(), px, py, 2))) {
            float p = seek_progress_from_x(px);
            if (!s_scrubbing || p != s_seek_progress)
                s_dirty = 1;
            s_seek_progress = p;
            s_scrubbing     = 1;
            s_press_id      = -1;
            s_eq_press      = 0;
            s_display_press = 0;
        } else if (ui_rect_contains(bot_eq_rect(), px, py, 2)) {
            s_eq_press  = 1;
            s_press_id  = -1;
            s_scrubbing = 0;
            s_display_press = 0;
        } else if (!ftp_is_active()
                   && ui_rect_contains(ui_rect(BOT_DISPLAY_X, BOT_DISPLAY_Y,
                                               BOT_DISPLAY_W, BOT_DISPLAY_H),
                                       px, py, 0)) {
            s_display_press = 1;
            s_press_id      = -1;
            s_scrubbing     = 0;
            s_eq_press      = 0;
        } else {
            if (s_press_id >= 0)
                s_dirty = 1;
            s_press_id  = -1;
            s_scrubbing = 0;
            s_eq_press  = 0;
            s_display_press = 0;
        }
    } else {
        if (s_last_touch_held) {
            if (s_scrubbing) {
                (void)audio_seek_ratio(s_seek_progress);
                s_dirty = 1;
            } else if (s_press_id >= 0) {
                activate_widget(s_press_id);
            } else if (s_eq_press) {
                s_eq_mode = !s_eq_mode;
                audio_viz_set_scope_mode(s_eq_mode);
                toast_show(s_eq_mode ? "WAVE" : "BARS");
                s_dirty = 1;
            } else if (s_display_press) {
                const char *playing = audio_current_path();
                const char *selected = musiclist_selected_path();

                if (playing != NULL
                    && (selected == NULL || strcmp(selected, playing) != 0))
                    (void)musiclist_select_path(playing);
            }
        }
        if (s_press_id >= 0 || s_scrubbing)
            s_dirty = 1;
        s_press_id  = -1;
        s_scrubbing = 0;
        s_eq_press  = 0;
        s_display_press = 0;
    }

    if (tdown) {
        s_last_x = px;
        s_last_y = py;
    }
    s_last_touch_held = tdown;

    meta_update();
    update_title_scroll();

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

    /* EQ bars: ease displayed levels toward the live targets every frame so
     * the ~11 Hz analysis rate doesn't show as steps; repaint only when a
     * bar's pixel height actually changes. Idle/paused eases down to zero. */
    if (s_eq_mode == 0) {
        uint8_t target[AUDIO_VIZ_BARS];
        int     i, any = 0;

        if (playing)
            audio_viz_read(target);
        else
            memset(target, 0, sizeof(target));

        for (i = 0; i < AUDIO_VIZ_BARS; i++) {
            int cur = s_eq_show[i];
            int t   = (int)target[i];
            int h;

            if (t > cur)
                cur = t;                  /* instant attack */
            else if (t < cur)
                cur -= (cur - t + 2) / 3; /* fast release */
            s_eq_show[i] = cur;
            if (cur > 0)
                any = 1;

            h = (cur * BOT_EQ_H + 127) / 255;
            if (h != s_eq_h[i]) {
                s_eq_h[i] = h;
                s_dirty   = 1;
            }
        }
        if (any != s_eq_live) {
            s_eq_live = any;
            s_dirty   = 1;
        }
    } else {
        /* Waveform: the viz module streams decimated points; slide our
         * 168-point window forward at the playback rate every frame so the
         * sweep runs at 60 fps instead of stepping once per audio chunk.
         * Points land in bursts (one whole chunk at a time), so the cursor
         * trails the published total by one max-size chunk and advances at
         * the constant sample-clock rate; a tiny correction term absorbs
         * vsync-vs-audio clock drift without visible speed wobble. */
        int8_t snap[AUDIO_VIZ_SCOPE];

        if (playing) {
            const float lag = (float)(4096 / AUDIO_VIZ_SCOPE_DECIM + 8);
            uint32_t total  = audio_viz_scope_total();
            int32_t  sr     = audio_ctrl_sample_rate();
            float    rate   = ((sr > 0) ? (float)sr : 44100.f)
                              / ((float)AUDIO_VIZ_SCOPE_DECIM * 60.f);
            float    target = (float)total - lag;
            float    err;

            if (target < 0.f)
                target = 0.f;
            s_eq_scope_pos += rate;
            err = target - s_eq_scope_pos;
            if (err < -512.f || err > 512.f)
                s_eq_scope_pos = target; /* seek/track change: snap */
            else
                s_eq_scope_pos += err * 0.01f;
            if (s_eq_scope_pos < 0.f)
                s_eq_scope_pos = 0.f;

            /* Oscilloscope trigger: anchor the displayed window on a rising
             * zero crossing near the playback cursor. Without this the
             * window contents shift left by a constant amount every frame,
             * which reads as fast sideways scrolling; with it, periodic
             * content holds its phase in place and visibly pulses and
             * reshapes with the music. */
            {
                uint32_t p = (uint32_t)s_eq_scope_pos;

                if (p >= 2u * AUDIO_VIZ_SCOPE) {
                    int8_t recent[2 * AUDIO_VIZ_SCOPE];
                    int    start = AUDIO_VIZ_SCOPE, k;

                    audio_viz_scope_window(p - AUDIO_VIZ_SCOPE, recent);
                    audio_viz_scope_window(p, recent + AUDIO_VIZ_SCOPE);
                    for (k = 0; k < AUDIO_VIZ_SCOPE; k++) {
                        if (recent[k] <= 0 && recent[k + 1] > 0) {
                            start = k + 1;
                            break;
                        }
                    }
                    memcpy(snap, recent + start, sizeof(snap));
                } else {
                    audio_viz_scope_window(p, snap);
                }
            }
        } else {
            s_eq_scope_pos = 0.f;
            memset(snap, 0, sizeof(snap));
        }

        if (memcmp(snap, s_eq_scope, sizeof(snap)) != 0) {
            memcpy(s_eq_scope, snap, sizeof(snap));
            s_dirty = 1;
        }
        if (playing != s_eq_live) {
            s_eq_live = playing;
            s_dirty   = 1;
        }
    }

    {
        int pos_s = 0, dur_s = 0;
        int valid = (playing || paused) && audio_time_seconds(&pos_s, &dur_s);
        if (valid && s_scrubbing)
            pos_s = (int)(s_seek_progress * (float)dur_s);
        if (valid != s_time_valid || pos_s != s_time_pos || dur_s != s_time_dur) {
            s_time_valid = valid;
            s_time_pos   = pos_s;
            s_time_dur   = dur_s;
            s_dirty      = 1;
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
        present_bottom(bottom);
        s_dirty = 0;
    }
}

#undef FB_PX
