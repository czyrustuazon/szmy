#pragma once

#include <3ds.h>

/* SD card path for Root:/music */
#define MUSIC_DIR_FS    "sdmc:/music"
#define MUSIC_DIR_LABEL "Root:/music"
#define MUSIC_PATH_MAX  256

/* Open MUSIC_DIR_FS. Returns 0 on success, negative if the folder is missing. */
int musiclist_init(void);

void musiclist_exit(void);

int musiclist_count(void);

int musiclist_get_selected(void);

void musiclist_select_prev(void);
void musiclist_select_next(void);

/* True when the selected entry is a subfolder. */
int musiclist_selected_is_dir(void);

/* Full path of the selected entry, or NULL if the list is empty. */
const char *musiclist_selected_path(void);

/* Enter the selected folder. Returns 0 on success, negative on error. */
int musiclist_enter(void);

/* Go up one level (stops at MUSIC_DIR_FS). Returns 0 if moved, -1 at root. */
int musiclist_go_back(void);

/* Current folder path (sdmc:/...). */
const char *musiclist_cwd(void);

/* Redraw the list on the top screen console. Pass playback state for the status line. */
void musiclist_draw(PrintConsole *top, int playing, int paused);
