#pragma once

#include <3ds/console.h>

/* Load embedded gfx/top_screen_bg.bmp, scale to top screen, draw once. */

int  topbg_init(void);
/* Must use the same framebuffer pointer as PrintConsole (see top->frameBuffer). */
void topbg_blit_to_top(PrintConsole *top);
