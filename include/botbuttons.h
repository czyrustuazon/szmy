#pragma once

#include <3ds.h>

/* Bottom UI: chrome BMP for buttons (hitboxes only) + seek overlays.
 * Trash starts a delete confirm handled via A/B on the top screen. */
void botbuttons_init(PrintConsole *bottom);

/* Per frame: touch, scrubber drag, redraw when dirty. */
void botbuttons_frame(PrintConsole *bottom);

/* True while waiting for A=confirm / B=cancel on a delete. */
int botbuttons_confirm_active(void);

void botbuttons_confirm_accept(void);
void botbuttons_confirm_cancel(void);
