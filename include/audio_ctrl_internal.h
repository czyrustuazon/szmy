#ifndef AUDIO_CTRL_INTERNAL_H
#define AUDIO_CTRL_INTERNAL_H

void audio_ctrl_on_playback_start(void);
void audio_ctrl_on_playback_end(int result, int was_stopped);
void audio_ctrl_prepare_async(int keep_resume);
void audio_ctrl_after_stop_wait(void);
void audio_ctrl_after_seek_restart(void);
void audio_ctrl_clear_exit_flags(void);
void audio_ctrl_set_paused_flag(int paused);
void audio_ctrl_set_playing_flag(int playing);
void audio_ctrl_clear_timeline(void);

#endif /* AUDIO_CTRL_INTERNAL_H */
