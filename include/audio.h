#ifndef AUDIO_H
#define AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize NDSP (call once at startup). Returns 0 on success. */
int audio_init(void);

/* Shutdown NDSP. */
void audio_exit(void);

/* Play a file (vgmstream formats + FLAC via dr_flac, MP3 when libmpg123 is linked).
 * Path is UTF-8, e.g. "sdmc:/music.wav". Blocks until playback finishes or stop is requested.
 * Returns 0 on success, negative on error (file not found, unsupported format, etc.). */
int audio_play_file(const char *path);

/* Play a FLAC file (used internally when path ends with .flac). */
int audio_play_flac(const char *path);

/* Request playback to stop (call from another thread or signal; playback thread will exit). */
void audio_stop(void);

/* Returns true if user requested stop (used internally by playback). */
int audio_should_stop(void);

/* Start playback in background; returns immediately. Main loop can process input while playing.
 * Returns 0 on success, negative on error. Use audio_stop() to stop, audio_is_playing() to check. */
int audio_play_file_async(const char *path);

/* True if playback is currently running (async). */
int audio_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
