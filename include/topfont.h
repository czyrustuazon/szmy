#pragma once

#include <3ds.h>

/* Install libctru PrintConsole text drawing that only writes foreground (glyph) pixels
 * so a pre-blit top background (e.g. topbg_blit_to_top) stays visible in "empty" cells. */
void topfont_enable_transparent_bg(PrintConsole *top);
