#include "softfont.h"
#include "bmp_util.h"

#include <3ds.h>
#include <3ds/font.h>
#include <3ds/util/utf.h>

/* Same pixel layout as the botbuttons compose buffer (240-high columns,
 * y flipped). */
#define SF_W 320
#define SF_H 240
#define SF_PX(fb, x, y) ((fb)[(u32)(x) * 240u + (239u - (u32)(y))])

static CFNT_s *s_font;
static int     s_state; /* 0 = untried, 1 = ok, -1 = unavailable */

int softfont_init(void)
{
    FINF_s *finf;
    TGLP_s *tglp;

    if (s_state != 0)
        return s_state == 1 ? 0 : -1;
    s_state = -1;

    if (R_FAILED(fontEnsureMapped()))
        return -1;
    s_font = fontGetSystemFont();
    if (s_font == NULL)
        return -1;
    finf = fontGetInfo(s_font);
    if (finf == NULL || finf->tglp == NULL)
        return -1;
    tglp = finf->tglp;
    /* The shared system font keeps its glyph sheets in A4 (4-bit alpha);
     * anything else would need a different texel decoder. */
    if (tglp->sheetFmt != GPU_A4)
        return -1;

    s_state = 1;
    return 0;
}

int softfont_ok(void)
{
    if (s_state == 0)
        (void)softfont_init();
    return s_state == 1;
}

/* Alpha (0..15) of one texel. Sheets are swizzled GPU textures: 8x8 tiles
 * in Morton order, stored top-down in memory (confirmed on hardware),
 * two texels per byte, low nibble first. */
static u8 sheet_alpha4(const u8 *sheet, const TGLP_s *tglp, u32 x, u32 ytop)
{
    u32 m = ytop;
    u32 tiles_per_row = (u32)tglp->sheetWidth / 8u;
    u32 tile = (m / 8u) * tiles_per_row + (x / 8u);
    u32 tx = x & 7u, ty = m & 7u;
    u32 mort = (tx & 1u) | ((ty & 1u) << 1) | ((tx & 2u) << 1)
             | ((ty & 2u) << 2) | ((tx & 4u) << 2) | ((ty & 4u) << 3);
    u32 idx  = tile * 64u + mort;
    u8  b    = sheet[idx >> 1];

    return (idx & 1u) ? (u8)(b >> 4) : (u8)(b & 0x0F);
}

float softfont_line_height(float scale)
{
    if (!softfont_ok())
        return 0.f;
    return (float)fontGetInfo(s_font)->lineFeed * scale;
}

float softfont_width(float scale, const char *utf8)
{
    const u8 *p = (const u8 *)utf8;
    float     w = 0.f;

    if (!softfont_ok() || utf8 == NULL)
        return 0.f;
    for (;;) {
        u32     cp;
        ssize_t n = decode_utf8(&cp, p);
        if (n <= 0 || cp == 0)
            break;
        p += n;
        w += scale * (float)fontGetCharWidthInfo(
                 s_font, fontGlyphIndexFromCodePoint(s_font, cp))->charWidth;
    }
    return w;
}

/* Draw one glyph cell scaled; box-averages the source texels per destination
 * pixel so downscaled edges stay smooth (16 alpha levels give clean AA). */
static void draw_glyph(uint16_t *fb, const TGLP_s *tglp, const u8 *sheet,
                       u32 cx, u32 cy, int gw, float dst_x, float dst_y,
                       float scale, u8 cr, u8 cg, u8 cb,
                       int clip_x0, int clip_x1)
{
    int dw = (int)((float)gw * scale + 0.5f);
    int dh = (int)((float)tglp->cellHeight * scale + 0.5f);
    int x0 = (int)(dst_x + 0.5f);
    int y0 = (int)(dst_y + 0.5f);
    int dx, dy;

    if (dw <= 0 || dh <= 0)
        return;

    for (dy = 0; dy < dh; dy++) {
        int sy0 = (int)((float)dy / scale);
        int sy1 = (int)((float)(dy + 1) / scale);
        int py  = y0 + dy;

        if (py < 0 || py >= SF_H)
            continue;
        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        if (sy1 > (int)tglp->cellHeight)
            sy1 = (int)tglp->cellHeight;
        for (dx = 0; dx < dw; dx++) {
            int sx0 = (int)((float)dx / scale);
            int sx1 = (int)((float)(dx + 1) / scale);
            int px  = x0 + dx;
            u32 sum = 0, cnt = 0;
            int sx, sy;
            u32 a;

            if (px < 0 || px >= SF_W || px < clip_x0 || px >= clip_x1)
                continue;
            if (sx1 <= sx0)
                sx1 = sx0 + 1;
            if (sx1 > gw)
                sx1 = gw;
            for (sy = sy0; sy < sy1; sy++)
                for (sx = sx0; sx < sx1; sx++) {
                    sum += sheet_alpha4(sheet, tglp,
                                        cx + (u32)sx, cy + (u32)sy);
                    cnt++;
                }
            if (cnt == 0)
                continue;
            a = (sum * 17u) / cnt; /* 0..15 -> 0..255 */
            if (a == 0)
                continue;
            SF_PX(fb, px, py) =
                bmp_mix565(SF_PX(fb, px, py), cr, cg, cb, a);
        }
    }
}

float softfont_draw_clipped(uint16_t *fb, float x, float y, float scale,
                            uint16_t color, const char *utf8,
                            float x_min, float x_max)
{
    const u8 *p = (const u8 *)utf8;
    FINF_s   *finf;
    TGLP_s   *tglp;
    float     pen = x;
    int       chars_per_sheet;
    u8        cr, cg, cb;

    if (!softfont_ok() || fb == NULL || utf8 == NULL)
        return 0.f;

    finf = fontGetInfo(s_font);
    tglp = finf->tglp;
    chars_per_sheet = tglp->nRows * tglp->nLines;

    cr = (u8)(((color >> 11) & 31) * 255 / 31);
    cg = (u8)(((color >> 5) & 63) * 255 / 63);
    cb = (u8)((color & 31) * 255 / 31);

    for (;;) {
        u32              cp;
        ssize_t          n = decode_utf8(&cp, p);
        int              gi, sheet_id, in_sheet, line_id, row_id;
        charWidthInfo_s *cwi;
        u32              cx, cy;

        if (n <= 0 || cp == 0)
            break;
        p += n;

        gi  = fontGlyphIndexFromCodePoint(s_font, cp);
        cwi = fontGetCharWidthInfo(s_font, gi);

        if (pen + scale * (float)cwi->charWidth > x_max)
            break;

        sheet_id = gi / chars_per_sheet;
        in_sheet = gi % chars_per_sheet;
        line_id  = in_sheet / tglp->nRows;
        row_id   = in_sheet % tglp->nRows;
        cx = (u32)row_id * ((u32)tglp->cellWidth + 1u) + 1u;
        cy = (u32)line_id * ((u32)tglp->cellHeight + 1u) + 1u;

        if (cwi->glyphWidth > 0)
            draw_glyph(fb, tglp,
                       tglp->sheetData + (u32)sheet_id * tglp->sheetSize,
                       cx, cy, cwi->glyphWidth,
                       pen + scale * (float)cwi->left, y,
                       scale, cr, cg, cb, (int)x_min, (int)x_max);
        pen += scale * (float)cwi->charWidth;
    }
    return pen - x;
}

float softfont_draw(uint16_t *fb, float x, float y, float scale,
                    uint16_t color, const char *utf8, float x_max)
{
    return softfont_draw_clipped(
        fb, x, y, scale, color, utf8, 0.f, x_max);
}
