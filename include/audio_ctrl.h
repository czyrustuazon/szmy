#ifndef AUDIO_CTRL_H
#define AUDIO_CTRL_H

#include <stdint.h>
#include <stddef.h>

void audio_ctrl_reset(void);

void audio_stop(void);
void audio_pause(void);

int  audio_should_stop(void);
int  audio_playback_should_exit(void);
int  audio_end_is_pause(void);
int  audio_is_playing(void);
int  audio_is_paused(void);

int64_t audio_take_resume_sample(void);
void    audio_note_paused_at(int64_t sample);
void    audio_ctrl_set_resume_sample(int64_t sample);

void    audio_ctrl_set_duration(int64_t samples);
void    audio_ctrl_set_position(int64_t samples);
void    audio_ctrl_set_sample_rate(int32_t rate);
int32_t audio_ctrl_sample_rate(void);
int64_t audio_ctrl_duration_samples(void);
int64_t audio_ctrl_position_samples(void);
float   audio_progress_ratio(void);

/* Elapsed/total whole seconds; 0 if duration or sample rate is unknown. */
int  audio_time_seconds(int *pos_sec, int *dur_sec);
/* Render seconds as "M:SS" (minutes unpadded). */
void audio_format_mmss(int seconds, char *out, size_t out_size);

int  audio_last_play_error(void);
void audio_set_play_error(int error);
void audio_clear_play_error(void);

#ifdef UNIT_TEST
void audio_ctrl_test_set_playing(int playing);
void audio_ctrl_test_set_paused(int paused);
void audio_ctrl_test_set_request_flags(int stop, int pause);
#endif

#endif /* AUDIO_CTRL_H */
