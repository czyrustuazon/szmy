#pragma once

#include <3ds.h>

/* 3DS system font (CJK) via citro2d. */

#define JPTEXT_LINE_H  14.0f
#define JPTEXT_X       8.0f

int  jptext_init(void);
void jptext_exit(void);

int  jptext_ok(void);
int  jptext_init_error(void);
const char *jptext_init_error_str(void);

/* Call once per frame before drawing any jptext strings. */
void jptext_begin(void);
void jptext_end(void);

float jptext_line_y(int line);

void jptext_draw(float x, float y, u32 color, const char *utf8);
