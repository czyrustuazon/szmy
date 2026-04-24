#include "topfont.h"
#include <3ds/console.h>
#include <3ds/gfx.h>

/* Mirror libctru source/console.c color table for ANSI fg resolution. */
static const u16 s_colorTable[] = {
    RGB8_to_565(0, 0, 0),
    RGB8_to_565(128, 0, 0),
    RGB8_to_565(0, 128, 0),
    RGB8_to_565(128, 128, 0),
    RGB8_to_565(0, 0, 128),
    RGB8_to_565(128, 0, 128),
    RGB8_to_565(0, 128, 128),
    RGB8_to_565(192, 192, 192),
    RGB8_to_565(128, 128, 128),
    RGB8_to_565(255, 0, 0),
    RGB8_to_565(0, 255, 0),
    RGB8_to_565(255, 255, 0),
    RGB8_to_565(0, 0, 255),
    RGB8_to_565(255, 0, 255),
    RGB8_to_565(0, 255, 255),
    RGB8_to_565(255, 255, 255),
    RGB8_to_565(0, 0, 0),
    RGB8_to_565(64, 0, 0),
    RGB8_to_565(0, 64, 0),
    RGB8_to_565(64, 64, 0),
    RGB8_to_565(0, 0, 64),
    RGB8_to_565(64, 0, 64),
    RGB8_to_565(0, 64, 64),
    RGB8_to_565(96, 96, 96),
};

static void topfont_draw_glyphs_only(PrintConsole *pc, int c) {
    c -= (int)pc->font.asciiOffset;
    if (c < 0 || c > (int)pc->font.numChars)
        return;

    u8 *fontdata = pc->font.gfx + (8 * c);
    u16         fg  = pc->fg;
    u16         bg  = pc->bg;

    if (!(pc->flags & CONSOLE_FG_CUSTOM)) {
        if (pc->flags & (CONSOLE_COLOR_BOLD | CONSOLE_COLOR_FG_BRIGHT)) {
            fg += 8;
        } else if (pc->flags & CONSOLE_COLOR_FAINT) {
            fg += 16;
        }
        fg = s_colorTable[fg];
    }

    if (!(pc->flags & CONSOLE_BG_CUSTOM)) {
        if (pc->flags & CONSOLE_COLOR_BG_BRIGHT)
            bg += 8;
        bg = s_colorTable[bg];
    }

    if (pc->flags & CONSOLE_COLOR_REVERSE) {
        u16 t = fg;
        fg  = bg;
        bg  = t;
    }
    (void)bg;

    u8 b1 = *(fontdata++);
    u8 b2 = *(fontdata++);
    u8 b3 = *(fontdata++);
    u8 b4 = *(fontdata++);
    u8 b5 = *(fontdata++);
    u8 b6 = *(fontdata++);
    u8 b7 = *(fontdata++);
    u8 b8 = *(fontdata++);

    if (pc->flags & CONSOLE_UNDERLINE)
        b8 = 0xff;
    if (pc->flags & CONSOLE_CROSSED_OUT)
        b4 = 0xff;

    u8  mask  = 0x80;
    int i;

    int   x     = (pc->cursorX - 1 + pc->windowX - 1) * 8;
    int   y     = (pc->cursorY - 1 + pc->windowY - 1) * 8;
    u16 *screen = &pc->frameBuffer[(x * 240) + (239 - (y + 7))];

    for (i = 0; i < 8; i++) {
        if (b8 & mask) {
            *(screen++) = fg;
        } else
            screen++;
        if (b7 & mask) {
            *(screen++) = fg;
        } else
            screen++;
        if (b6 & mask) {
            *(screen++) = fg;
        } else
            screen++;
        if (b5 & mask) {
            *(screen++) = fg;
        } else
            screen++;
        if (b4 & mask) {
            *(screen++) = fg;
        } else
            screen++;
        if (b3 & mask) {
            *(screen++) = fg;
        } else
            screen++;
        if (b2 & mask) {
            *(screen++) = fg;
        } else
            screen++;
        if (b1 & mask) {
            *(screen++) = fg;
        } else
            screen++;
        mask >>= 1;
        screen += 240 - 8;
    }
}

/* Same as libctru console.c consoleClearLine case 2, but with glyph-only space draws. */
static void topfont_clear_line_k2(PrintConsole *pc) {
    int colTemp = pc->cursorX;
    int i;
    pc->cursorX = 1;
    for (i = 0; i < pc->windowWidth; i++) {
        topfont_draw_glyphs_only(pc, (int) ' ');
        pc->cursorX++;
    }
    pc->cursorX = colTemp;
    gfxFlushBuffers();
}

/* Same as libctru newRow(), with scroll + clear; uses PrintConsole* instead of currentConsole. */
static void topfont_new_row(PrintConsole *pc) {
    pc->cursorY++;
    if (pc->cursorY > pc->windowHeight) {
        pc->cursorY  = pc->windowHeight;
        u16 *dst     = &pc->frameBuffer[((pc->windowX - 1) * 8 * 240) + (239 - ((pc->windowY - 1) * 8))];
        u16 *src     = dst - 8;
        int  i, j;
        for (i = 0; i < (pc->windowWidth) * 8; i++) {
            u32 *from = (u32 *)((int)src & ~3);
            u32 *to   = (u32 *)((int)dst & ~3);
            for (j = 0; j < (((pc->windowHeight - 1) * 8) / 2); j++) {
                *(to--) = *(from--);
            }
            dst += 240;
            src += 240;
        }
        topfont_clear_line_k2(pc);
    }
}

static bool topfont_print_char(void *con, int c) {
    PrintConsole *pc = (PrintConsole *)con;

    if (c == 9 || c == 10 || c == 13)
        return false;

    if (c == 8) {
        pc->cursorX--;
        if (pc->cursorX < 1) {
            if (pc->cursorY > 1) {
                pc->cursorX  = pc->windowWidth;
                pc->cursorY--;
            } else
                pc->cursorX = 1;
        }
        topfont_draw_glyphs_only(pc, (int) ' ');
        return true;
    }

    if (pc->cursorX > pc->windowWidth) {
        pc->cursorX = 1;
        topfont_new_row(pc);
    }
    topfont_draw_glyphs_only(pc, c);
    pc->cursorX++;
    return true;
}

void topfont_enable_transparent_bg(PrintConsole *top) {
    if (top) {
        top->PrintChar = topfont_print_char;
    }
}
