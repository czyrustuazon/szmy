#pragma once

#include <3ds.h>

/* Bottom-screen play / pause graphics (y >= 160). Press = show *_active, release = *_inactive. */
void botbuttons_init(PrintConsole *bottom);

/* Per frame: hit-test, draw strip + icons (alpha), optional play/pause on touch release. */
void botbuttons_frame(PrintConsole *bottom);
