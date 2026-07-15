#include "jptext.h"
#include "jptext_errors.h"
#include "topbg.h"
#include <citro2d.h>
#include <citro3d.h>
#include <3ds/font.h>
#include <3ds/services/cfgu.h>
#include <string.h>

#define TEXT_SCALE  0.5f
#define GLYPH_MAX   4096

static C2D_Font            s_font = NULL;
static C2D_TextBuf         s_buf  = NULL;
static C3D_RenderTarget   *s_top;
static int                 s_ok;
static int                 s_cfgu;
static int                 s_init_error;

static int init_system_font(void)
{
    u8 region = CFG_REGION_USA;

    if (R_SUCCEEDED(CFGU_SecureInfoGetRegion(&region)))
        s_font = C2D_FontLoadSystem((CFG_Region)region);
    else
        s_font = C2D_FontLoadSystem(CFG_REGION_USA);

    /* NULL = native region; citro2d uses fontGetSystemFont(). */
    if (R_FAILED(fontEnsureMapped()))
        return -1;
    return 0;
}

int jptext_init_error(void)
{
    return s_init_error;
}

const char *jptext_init_error_str(void)
{
    return jptext_error_str(s_init_error);
}

int jptext_ok(void)
{
    return s_ok;
}

int jptext_init(void)
{
    if (s_ok)
        return 0;

    s_init_error = 0;

    if (R_FAILED(cfguInit())) {
        s_init_error = 1;
        return -1;
    }
    s_cfgu = 1;

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        s_init_error = 2;
        goto fail;
    }

    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        s_init_error = 3;
        goto fail;
    }

    C2D_Prepare();

    /* Swizzled .t3x background (tex3ds); must be after C3D/C2D init. */
    if (topbg_init(1) != 0) {
        s_init_error = 7;
        goto fail;
    }

    s_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    if (!s_top) {
        s_init_error = 4;
        goto fail;
    }

    if (init_system_font() != 0) {
        s_init_error = 5;
        goto fail;
    }

    s_buf = C2D_TextBufNew(GLYPH_MAX);
    if (!s_buf) {
        s_init_error = 6;
        goto fail;
    }

    s_ok = 1;
    return 0;

fail:
    if (s_buf) {
        C2D_TextBufDelete(s_buf);
        s_buf = NULL;
    }
    if (s_font) {
        C2D_FontFree(s_font);
        s_font = NULL;
    }
    if (s_top) {
        s_top = NULL;
    }
    topbg_exit();
    C2D_Fini();
    C3D_Fini();
    if (s_cfgu) {
        cfguExit();
        s_cfgu = 0;
    }
    return -1;
}

void jptext_exit(void)
{
    if (!s_ok)
        return;
    C2D_TextBufDelete(s_buf);
    s_buf = NULL;
    if (s_font)
        C2D_FontFree(s_font);
    s_font = NULL;
    topbg_exit();
    C2D_Fini();
    C3D_Fini();
    if (s_cfgu) {
        cfguExit();
        s_cfgu = 0;
    }
    s_top  = NULL;
    s_buf  = NULL;
    s_font = NULL;
    s_ok   = 0;
}

void jptext_begin(void)
{
    if (!s_ok)
        return;
    C2D_TargetClear(s_top, C2D_Color32(0x00, 0x00, 0x00, 0xFF));
    C2D_SceneBegin(s_top);
}

void jptext_end(void)
{
    if (!s_ok)
        return;
    C2D_Flush();
}

float jptext_line_y(int line)
{
    return (float)line * JPTEXT_LINE_H;
}

void jptext_draw(float x, float y, u32 color, const char *utf8)
{
    C2D_Text text;

    if (!s_ok || !utf8 || !utf8[0])
        return;

    C2D_TextBufClear(s_buf);
    C2D_TextFontParse(&text, s_font, s_buf, utf8);
    C2D_TextOptimize(&text);
    C2D_DrawText(
        &text, C2D_AlignLeft | C2D_WithColor,
        x, y, 0.5f, TEXT_SCALE, TEXT_SCALE, color);
}
