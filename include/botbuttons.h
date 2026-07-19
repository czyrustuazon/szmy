#pragma once

#include <3ds.h>

/* Bottom UI: chrome BMP for buttons (hitboxes only) + seek overlays.
 * Trash starts a delete confirm handled via A/B on the top screen. */
void botbuttons_init(PrintConsole *bottom);

/* Per frame: touch, scrubber drag, redraw when dirty. */
void botbuttons_frame(PrintConsole *bottom);

/* Show a message in the same notification box used by touch controls. */
void botbuttons_notify(const char *message);

/* True while waiting for A=confirm / B=cancel on a delete. */
int botbuttons_confirm_active(void);

void botbuttons_confirm_accept(void);
void botbuttons_confirm_cancel(void);

/* Delete path immediately with no confirmation UI. Same next-track handoff
 * as confirming trash. Copies path before stopping playback. 0 = ok. */
int botbuttons_delete_now(const char *path, int is_dir);

/* Repeat mode: 0 = off, 1 = repeat current track, 2 = repeat all. */
int botbuttons_repeat_mode(void);

/* True while shuffle is enabled (random no-repeat cycle). */
int botbuttons_shuffle_on(void);
