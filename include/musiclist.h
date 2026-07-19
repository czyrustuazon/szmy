#pragma once

#include <3ds.h>
#include "music_paths.h"

/* Open a folder and scan entries. Returns 0 on success. */
int musiclist_open(const char *dir);

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

/* Open the containing folder and move the cursor to `path`.
 * Returns 0 when the playable file was found, -1 otherwise. */
int musiclist_select_path(const char *path);

/* Enter the selected folder. Returns 0 on success, negative on error. */
int musiclist_enter(void);

/* Path to play for the current selection, or NULL if a folder/nothing is selected. */
const char *musiclist_play_path(void);

/* Enter folder, or signal that a file is selected (0=file, 1=entered, -1=nothing). */
int musiclist_activate(void);

/* Go up one level (stops at MUSIC_DIR_FS). Returns 0 if moved, -1 at root. */
int musiclist_go_back(void);

/* Current folder path (sdmc:/...). */
const char *musiclist_cwd(void);

/* Override status line (e.g. delete confirm). NULL clears.
 * help may be NULL to use "A=yes  B=cancel". */
void musiclist_set_prompt(const char *msg, const char *help);

/* Delete an entry on SD and refresh the current folder list. Directories are
 * removed recursively. 0 = ok. */
int musiclist_delete_entry(const char *path, int is_dir);

/* Backward-compatible file-only wrapper. */
int musiclist_delete_file(const char *path);

/* Rescan the current folder without resetting the library root. 0 = ok. */
int musiclist_refresh(void);

/* Select next/prev audio file after `path` (depth-first across folders).
 * Climbs toward music root when a folder ends; does not wrap. Updates selection.
 * NULL if none. */
const char *musiclist_next_file_after(const char *path);
const char *musiclist_prev_file_before(const char *path);

/* First playable file in the whole library (from the music root), entering
 * subfolders as needed. Used for repeat-all wraparound. NULL if none. */
const char *musiclist_first_file(void);

/* Shuffle cycle: snapshot every file in the library in random order.
 * Each track is handed out exactly once per cycle.
 *
 * musiclist_shuffle_start begins a new cycle (cancelling any in-progress
 * build), scans one folder, then returns: >0 bag ready with that many tracks,
 * 0 empty, -1 still building (call musiclist_shuffle_poll each frame).
 * musiclist_shuffle_poll advances an in-progress build by one folder; returns
 * -1 if still going, or >=0 when the bag is ready (same meaning as start).
 * musiclist_shuffle_building is true between start and the finishing poll.
 * musiclist_shuffle_next returns the next unplayed track, or NULL when the
 * cycle is exhausted; call start again to begin a new cycle.
 * musiclist_shuffle_mark_played counts a manually chosen track as played so
 * it does not come up again this cycle (no-op if unknown/already played).
 * musiclist_shuffle_forget drops a deleted file (or every file under a
 * deleted folder) from the cycle so it can never be handed out again.
 * musiclist_shuffle_stop discards the cycle, any in-progress build, and
 * played history. */
int  musiclist_shuffle_start(void);
int  musiclist_shuffle_poll(void);
void musiclist_shuffle_stop(void);
int  musiclist_shuffle_active(void);
int  musiclist_shuffle_building(void);
const char *musiclist_shuffle_next(void);
void musiclist_shuffle_mark_played(const char *path);
void musiclist_shuffle_forget(const char *path);

/* Redraw the list on the top screen console. Pass playback state for the status line. */
void musiclist_draw(PrintConsole *top, int playing, int paused);
