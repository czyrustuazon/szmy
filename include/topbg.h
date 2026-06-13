#pragma once

#include <3ds.h>

/* Embedded top-screen background, drawn with citro2d (GPU texture). */

int  topbg_init(int gpu);
void topbg_exit(void);
int  topbg_ok(void);

/* Draw inside an active citro2d scene (between jptext_begin/end). */
void topbg_draw_full(void);
void topbg_draw_region(int y0, int y1);
