#ifndef AUDIO_VIZ_H
#define AUDIO_VIZ_H

#include <stdint.h>
#include <stddef.h>

/* Equalizer visualizer: band levels computed from PCM on the playback
 * thread, read by the UI thread. Lock-free single-writer/single-reader —
 * bar levels are independent bytes, so a torn read is visually harmless. */

#define AUDIO_VIZ_BARS  12
#define AUDIO_VIZ_SCOPE 168 /* waveform points, one per EQ-field pixel */

/* Waveform stream: one peak-preserving point per this many audio samples
 * (~2756 points/s at 44.1 kHz, so the 168-point window spans ~61 ms and
 * sweeps its full width in a few frames -- a lively wiggle at 60 fps
 * rather than a slow scroll). */
#define AUDIO_VIZ_SCOPE_DECIM 16

/* Players keep N_WAVEBUFS (4) chunks queued to NDSP, so a chunk passed to
 * audio_viz_feed only becomes audible ~3 chunks later. Feeds are held in a
 * FIFO this deep so the display matches what the speakers are playing. */
#define AUDIO_VIZ_DELAY 3

void audio_viz_reset(void);

/* UI thread: select which display is active (0 = bars, 1 = waveform).
 * Feeds compute only the active one, so the idle display costs nothing;
 * after a switch the new display fills in over the next few chunks.
 * Persists across audio_viz_reset (it is a UI preference). */
void audio_viz_set_scope_mode(int scope_on);

/* Playback thread: analyze an interleaved s16 chunk just fed to NDSP.
 * channels must be 1 or 2; ignores invalid input. */
void audio_viz_feed(const int16_t *pcm, size_t frames,
                    unsigned int channels, int sample_rate);

/* UI thread: copy current bar levels (0..255 each). */
void audio_viz_read(uint8_t out[AUDIO_VIZ_BARS]);

/* UI thread: total waveform points published so far (audible audio only). */
uint32_t audio_viz_scope_total(void);

/* UI thread: copy the AUDIO_VIZ_SCOPE points ending at end_total (clamped
 * to the published total). Missing/overwritten history reads as 0. */
void audio_viz_scope_window(uint32_t end_total, int8_t out[AUDIO_VIZ_SCOPE]);

#endif /* AUDIO_VIZ_H */
