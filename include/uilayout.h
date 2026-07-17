#ifndef UILAYOUT_H
#define UILAYOUT_H

/* Bottom-screen layout for 320x240 hybrid chrome (BG + overlays).
 * Hitboxes match gfx/bottom_screen_bg.bmp (from root bottom.png). */

#define BOT_W 320
#define BOT_H 240

typedef struct {
    int x, y, w, h;
} ui_rect_t;

static inline ui_rect_t ui_rect(int x, int y, int w, int h)
{
    ui_rect_t r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

static inline int ui_rect_contains(ui_rect_t r, int px, int py, int pad)
{
    return px >= r.x - pad && py >= r.y - pad
        && px < r.x + r.w + pad && py < r.y + r.h + pad;
}

static inline int ui_center_x(ui_rect_t parent, int child_w)
{
    return parent.x + (parent.w - child_w) / 2;
}

static inline int ui_center_y(ui_rect_t parent, int child_h)
{
    return parent.y + (parent.h - child_h) / 2;
}

/* --- Regions matching bottom.png @ 320x240 --- */
#define BOT_TITLE_Y        0
#define BOT_TITLE_H       20

#define BOT_DISPLAY_X     24
#define BOT_DISPLAY_Y     60
#define BOT_DISPLAY_W    272
#define BOT_DISPLAY_H    114

/* Seek groove under the display (empty in art — drawn in software). */
#define BOT_SEEK_X        28
#define BOT_SEEK_Y       176
#define BOT_SEEK_W       264
#define BOT_SEEK_H        18
#define BOT_SEEK_TRACK_X  32
#define BOT_SEEK_TRACK_Y 180
#define BOT_SEEK_TRACK_W 256
#define BOT_SEEK_TRACK_H   4
#define BOT_SEEK_THUMB_R   7
#define BOT_SEEK_THUMB_W  (BOT_SEEK_THUMB_R * 2)
#define BOT_SEEK_THUMB_H  (BOT_SEEK_THUMB_R * 2)
#define BOT_SEEK_TIME_W    0

typedef enum {
    BOT_WID_REPEAT = 0,
    BOT_WID_SHUFFLE,
    BOT_WID_DELETE,
    BOT_WID_PREV,
    BOT_WID_REWIND,   /* unused on current chrome */
    BOT_WID_PLAYPAUSE,
    BOT_WID_FF,       /* unused on current chrome */
    BOT_WID_NEXT,
    BOT_WID_COUNT
} bot_widget_id_t;

static inline ui_rect_t bot_band_seek(void)
{
    return ui_rect(BOT_SEEK_X, BOT_SEEK_Y, BOT_SEEK_W, BOT_SEEK_H);
}

static inline ui_rect_t bot_seek_track(void)
{
    return ui_rect(BOT_SEEK_TRACK_X, BOT_SEEK_TRACK_Y, BOT_SEEK_TRACK_W, BOT_SEEK_TRACK_H);
}

static inline ui_rect_t bot_seek_time_elapsed(void)
{
    return ui_rect(0, 0, 0, 0);
}

static inline ui_rect_t bot_seek_time_total(void)
{
    return ui_rect(0, 0, 0, 0);
}

/* progress in [0,1] → thumb rect centered on track. */
static inline ui_rect_t bot_seek_thumb(float progress)
{
    ui_rect_t track = bot_seek_track();
    int       span  = track.w - BOT_SEEK_THUMB_W;
    int       x;
    int       cy;

    if (progress < 0.f)
        progress = 0.f;
    if (progress > 1.f)
        progress = 1.f;
    x  = track.x + (int)(progress * (float)(span > 0 ? span : 0));
    cy = track.y + track.h / 2;
    return ui_rect(x, cy - BOT_SEEK_THUMB_R, BOT_SEEK_THUMB_W, BOT_SEEK_THUMB_H);
}

/*
 * Absolute hitboxes for bottom-clean.png @ 320x240:
 *   top:    status bar | trash
 *   middle: display panel, then seek groove under it
 *   bottom: repeat | prev | play/pause | next | shuffle
 */
static inline void bot_layout_all(ui_rect_t out[BOT_WID_COUNT])
{
    int i;

    for (i = 0; i < (int)BOT_WID_COUNT; i++)
        out[i] = ui_rect(0, 0, 0, 0);

    out[BOT_WID_DELETE]    = ui_rect(256, 28, 42, 36);
    out[BOT_WID_REPEAT]    = ui_rect(22, 198, 42, 40);
    out[BOT_WID_SHUFFLE]   = ui_rect(256, 198, 42, 40);
    out[BOT_WID_PREV]      = ui_rect(108, 200, 36, 36);
    out[BOT_WID_PLAYPAUSE] = ui_rect(144, 192, 44, 48);
    out[BOT_WID_NEXT]      = ui_rect(188, 200, 36, 36);
}

#endif /* UILAYOUT_H */
