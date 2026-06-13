#include "topbg.h"
#include <citro2d.h>
#include <string.h>
#include "top_screen_bg_t3x.h"

#define TOP_W 400
#define TOP_H 240

static int              s_ok;
static int              s_gpu;
static C2D_SpriteSheet  s_sheet;
static C2D_Image        s_image;

static int upload_gpu_texture(void)
{
    s_sheet = C2D_SpriteSheetLoadFromMem(
        top_screen_bg_t3x, top_screen_bg_t3x_size);
    if (!s_sheet)
        return -1;

    s_image = C2D_SpriteSheetGetImage(s_sheet, 0);
    s_gpu   = 1;
    return 0;
}

int topbg_init(int gpu)
{
    s_ok  = 0;
    s_gpu = 0;
    s_sheet = NULL;

    if (gpu && upload_gpu_texture() != 0)
        return -1;

    s_ok = 1;
    return 0;
}

int topbg_ok(void)
{
    return s_ok && s_gpu;
}

void topbg_exit(void)
{
    if (s_sheet) {
        C2D_SpriteSheetFree(s_sheet);
        s_sheet = NULL;
    }
    s_gpu = 0;
    s_ok  = 0;
}

void topbg_draw_full(void)
{
    C2D_Sprite spr;
    float      sx;
    float      sy;

    if (!topbg_ok())
        return;

    C2D_SpriteFromImage(&spr, s_image);
    C2D_SpriteSetPos(&spr, 0.0f, 0.0f);
    sx = (float)TOP_W / spr.image.subtex->width;
    sy = (float)TOP_H / spr.image.subtex->height;
    C2D_SpriteSetScale(&spr, sx, sy);
    C2D_DrawSprite(&spr);
}

void topbg_draw_region(int y0, int y1)
{
    (void)y0;
    (void)y1;
    topbg_draw_full();
}
