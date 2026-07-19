#ifndef SOFTFONT_H
#define SOFTFONT_H

#include <stdint.h>

/* CPU rasterizer for the 3DS system font (same BCFNT the top screen draws
 * via citro2d). The bottom screen is software-composed, so glyphs are read
 * straight from the mapped A4 glyph sheets and alpha-blended into the
 * 320x240 RGB565 bottom framebuffer (botbuttons pixel layout).
 *
 * scale is free-form: cells are ~30 px tall, 0.4-0.7 reads well on the
 * bottom screen. */

int softfont_init(void); /* 0 = ok; needs the shared system font mapped */
int softfont_ok(void);

/* Line advance (font lineFeed) at the given scale. */
float softfont_line_height(float scale);

/* Width of the string in pixels at the given scale (no drawing). */
float softfont_width(float scale, const char *utf8);

/* Draw UTF-8 text with its cell top at (x,y), clipped at x_max.
 * Returns the pen advance in pixels. */
float softfont_draw(uint16_t *fb, float x, float y, float scale,
                    uint16_t color, const char *utf8, float x_max);

/* As above, with clipping on both horizontal edges. Useful for scrolling
 * text whose pen starts to the left of its display region. */
float softfont_draw_clipped(uint16_t *fb, float x, float y, float scale,
                            uint16_t color, const char *utf8,
                            float x_min, float x_max);

#endif /* SOFTFONT_H */
