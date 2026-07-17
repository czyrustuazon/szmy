#ifndef AUDIO_H
#define AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Initialize NDSP (call once at startup). Returns 0 on success. */
int audio_init(void);

/* Shutdown NDSP. */
void audio_exit(void);

/* Play a file (vgmstream formats + FLAC via dr_flac + MP3 via minimp3).
 * Path is UTF-8, e.g. "sdmc:/music.wav". Blocks until playback finishes or stop is requested.
 * Returns 0 on success, negative on error (file not found, unsupported format, etc.). */
int audio_play_file(const char *path);

/* Play a FLAC file (used internally when path ends with .flac). */
int audio_play_flac(const char *path);

/* Play an MP3 file (used internally when path ends with .mp3/.mp2). */
int audio_play_mp3(const char *path);

/* Request playback to stop (call from another thread or signal; playback thread will exit). */
void audio_stop(void);

/* Pause at current position (playback thread exits cleanly; use audio_resume() to continue). */
void audio_pause(void);

/* Resume from a paused position. */
void audio_resume(void);

/* Returns true if user requested stop (used internally by playback). */
int audio_should_stop(void);

/* Returns true if playback should exit (stop or pause requested). */
int audio_playback_should_exit(void);

/* Start playback in background; returns immediately. Main loop can process input while playing.
 * Returns 0 on success, negative on error. Resumes if paused; use audio_stop() to fully stop. */
int audio_play_file_async(const char *path);

/* True if audio is actively playing. */
int audio_is_playing(void);

/* True if playback is paused and can be resumed. */
int audio_is_paused(void);

/* Path of the track currently playing or paused, or NULL if idle. */
const char *audio_current_path(void);

/* Last requested play path (still set after a natural end). */
const char *audio_last_path(void);

/* True once when a track finished on its own (not stop/pause). Consumes the flag. */
int audio_consume_ended_naturally(void);

/* Seek to ratio in [0,1] of the current track. 0 = ok, negative = idle/unknown length. */
int audio_seek_ratio(float ratio);

/* Current playback progress in [0,1], or 0 if duration is unknown. */
float audio_progress_ratio(void);

/* Stop playback and wait until the file handle is released. */
void audio_stop_wait(void);

/* Resume sample for next play (-1 = from start). Used internally by playback. */
int64_t audio_take_resume_sample(void);

/* Save resume point after a clean pause. Used internally by playback. */
void audio_note_paused_at(int64_t sample);

/* True when playback is exiting because of pause (not stop). */
int audio_end_is_pause(void);

/* Human-readable message for a playback error code (NULL if code is 0). */
const char *audio_error_message(int error);

/* Last playback error, or 0. Persists until the next play attempt. */
int audio_last_play_error(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
