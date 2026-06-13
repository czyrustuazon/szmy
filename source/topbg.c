#include "topbg.h"
#include <citro2d.h>
#include <citro3d.h>
#include <3ds.h>
#include <string.h>
#include "top_screen_bg_embed_bmp.h"

#define TOP_W  400
#define TOP_H  240
/* GPU textures must be power-of-two on 3DS. */
#define TEX_W  512
#define TEX_H  256

static int              s_ok;
static int              s_gpu;
static C3D_Tex          s_tex;
static Tex3DS_SubTexture s_subtex_full;
static C2D_Image        s_image;

static u32 u32le(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u32 u16le(const u8 *p)
{
    return (u32)(p[0] | (p[1] << 8));
}

static void bmp_bgr24_at(
    const u8 *bmp, u32 off_bits, u32 width, u32 height, int top_down, u32 row_bytes,
    u32 x, u32 y, u8 *b, u8 *g, u8 *r)
{
    u32 y_file = top_down ? y : (height - 1u - y);
    const u8 *row = bmp + off_bits + y_file * row_bytes;
    const u8 *px  = row + x * 3u;
    *b = px[0];
    *g = px[1];
    *r = px[2];
}

static void bmp_bgr32_at(
    const u8 *bmp, u32 off_bits, u32 width, u32 height, int top_down, u32 row_bytes,
    u32 x, u32 y, u8 *b, u8 *g, u8 *r)
{
    u32 y_file = top_down ? y : (height - 1u - y);
    const u8 *row = bmp + off_bits + y_file * row_bytes;
    const u8 *px  = row + x * 4u;
    *b = px[0];
    *g = px[1];
    *r = px[2];
}

static int validate_bmp(void)
{
    const u8 *data = top_screen_bg_embed_bmp;
    size_t    z    = top_screen_bg_embed_bmp_size;

    if (z < 54u)
        return -1;
    if (data[0] != 'B' || data[1] != 'M')
        return -1;

    u32 off_bits = u32le(data + 10);
    s32 ihead_sz = (s32)u32le(data + 14);
    u32 width    = u32le(data + 18);
    s32 iheight  = (s32)u32le(data + 22);
    u16 bpp      = (u16)u16le(data + 28);

    if (ihead_sz < 40 || width < 1u || iheight == 0 || (bpp != 24u && bpp != 32u))
        return -1;

    u32 height    = (u32)(iheight < 0 ? -iheight : iheight);
    u32 row_bytes = ((width * (u32)bpp + 31u) / 32u) * 4u;
    u32 need      = off_bits + row_bytes * height;
    if (need > z)
        return -1;

    (void)ihead_sz;
    return 0;
}

static int upload_gpu_texture(void)
{
    u32 *pixels;
    const u8 *data     = top_screen_bg_embed_bmp;
    u32       off_bits = u32le(data + 10);
    u32       width    = u32le(data + 18);
    s32       iheight  = (s32)u32le(data + 22);
    u16       bpp      = (u16)u16le(data + 28);
    u32       height   = (u32)(iheight < 0 ? -iheight : iheight);
    int       top_down = iheight < 0;
    u32       row_bytes = ((width * (u32)bpp + 31u) / 32u) * 4u;
    u32       x_max    = width  > 0u ? width  - 1u : 0u;
    u32       y_max    = height > 0u ? height - 1u : 0u;

    pixels = (u32 *)linearAlloc((size_t)TEX_W * (size_t)TEX_H * sizeof(u32));
    if (!pixels)
        return -1;
    memset(pixels, 0, (size_t)TEX_W * (size_t)TEX_H * sizeof(u32));

    for (u32 dy = 0; dy < TOP_H; dy++) {
        for (u32 dx = 0; dx < TOP_W; dx++) {
            u32 sx = (dx * x_max) / (TOP_W - 1u);
            u32 sy = (dy * y_max) / (TOP_H - 1u);
            u8  bb, gg, rr;

            if (bpp == 32u)
                bmp_bgr32_at(data, off_bits, width, height, top_down, row_bytes, sx, sy, &bb, &gg, &rr);
            else
                bmp_bgr24_at(data, off_bits, width, height, top_down, row_bytes, sx, sy, &bb, &gg, &rr);

            pixels[dy * TEX_W + dx] = C2D_Color32(rr, gg, bb, 0xFF);
        }
    }

    if (!C3D_TexInit(&s_tex, TEX_W, TEX_H, GPU_RGBA8)) {
        linearFree(pixels);
        return -1;
    }
    C3D_TexSetFilter(&s_tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexUpload(&s_tex, pixels);
    C3D_TexFlush(&s_tex);
    linearFree(pixels);

    s_subtex_full = (Tex3DS_SubTexture){
        TOP_W,
        TOP_H,
        0.0f,
        0.0f,
        (float)TOP_W / (float)TEX_W,
        (float)TOP_H / (float)TEX_H,
    };
    s_image = (C2D_Image){ &s_tex, &s_subtex_full };
    s_gpu   = 1;
    return 0;
}

int topbg_init(int gpu)
{
    s_ok  = 0;
    s_gpu = 0;

    if (validate_bmp() != 0)
        return -1;

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
    if (s_gpu) {
        C3D_TexDelete(&s_tex);
        s_gpu = 0;
    }
    s_ok = 0;
}

static void draw_subregion(int y0, int y1)
{
    Tex3DS_SubTexture sub;
    C2D_Image         img;
    C2D_DrawParams    params;
    float             v0;
    float             v1;

    if (!topbg_ok() || y0 < 0 || y1 <= y0)
        return;
    if (y0 >= TOP_H)
        return;
    if (y1 > TOP_H)
        y1 = TOP_H;

    v0 = (float)y0 / (float)TEX_H;
    v1 = (float)y1 / (float)TEX_H;

    sub = (Tex3DS_SubTexture){
        TOP_W,
        (u16)(y1 - y0),
        0.0f,
        v0,
        (float)TOP_W / (float)TEX_W,
        v1,
    };
    img = (C2D_Image){ &s_tex, &sub };
    params = (C2D_DrawParams){
        { 0.0f, (float)y0, (float)TOP_W, (float)(y1 - y0) },
        { 0.0f, 0.0f },
        0.5f,
        0.0f,
    };
    C2D_DrawImage(img, &params, NULL);
}

void topbg_draw_full(void)
{
    C2D_DrawParams params;

    if (!topbg_ok())
        return;

    params = (C2D_DrawParams){
        { 0.0f, 0.0f, (float)TOP_W, (float)TOP_H },
        { 0.0f, 0.0f },
        0.5f,
        0.0f,
    };
    C2D_DrawImage(s_image, &params, NULL);
}

void topbg_draw_region(int y0, int y1)
{
    draw_subregion(y0, y1);
}
