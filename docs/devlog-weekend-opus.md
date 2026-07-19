# Shipping szmy: equalizer, FTP, metadata, and Opus

*Dev notes from a dense weekend of 3DS homebrew work — what we built, what broke, and what actually fixed it.*

---

I’ve been building **szmy**, a multi-format music player for the Nintendo 3DS. Over the last stretch of sessions we went from “it plays files” to something that feels closer to a real portable player: visuals, file transfer, now-playing metadata, closed-lid behavior, and — today — Opus support.

A lot of that progress came from chasing bugs that looked like one thing and turned out to be another.

## What landed

### Playback UI that doesn’t fight you

- Accurate seek bar with current / total time under it (especially fixing MP3 duration estimates that used to stop short).
- Repeat-one / repeat-all, and shuffle that tracks a cycle so songs don’t repeat until the bag is empty.
- Shoulder buttons for previous / next; B for pause.
- Delete the *selected* item (files or folders), then auto-advance if you deleted what was playing.
- Tap the main panel to jump the playlist cursor back to the playing track.
- Small status chips for shuffle / repeat (same visual language as the breadbox toasts).

### Equalizer in the chrome

The empty strip between the folder and trash icons became a live visualizer:

- **Bars** — Goertzel band levels from real PCM
- **Wave** — oscilloscope-style trace, toggle by tapping the field

### FTP so the SD card can stay put

Tapping the folder icon starts a simple FTP server for the SD card. Connection details show on the top screen: `ftp://IP:5000`, username `szmy`, and a fresh 6-character password (ambiguous characters like `I` / `L` / `1` / `O` / `0` omitted). Playback stops while FTP is up; tap the folder again (or press B) to exit and refresh the playlist.

This wasn’t just a convenience feature — repeatedly yanking the SD card is how cards and slots die. Transfer over Wi‑Fi instead.

### Now-playing metadata

The grey panel on the bottom screen now shows cover art + tags:

- Embedded art when present (ID3 APIC / FLAC PICTURE), otherwise a generic music icon
- Title, filename, artist, album, and whatever else fits
- System-font soft-render on the bottom screen (so Japanese titles aren’t stuck on ASCII)
- Long titles scroll continuously

### Closed-lid playback

Music keeps going with the lid shut (headphones required — more on that below). Triple-tap while closed used to toggle play/pause; we later remapped that to a quick delete-without-confirm for the current track. When nothing is playing, the console is allowed to sleep normally.

### Making a fresh clone rebuild

We audited secrets / licenses, unified author naming, filled in missing sources and PNG→BMP fallbacks for MSYS builds without ImageMagick, and documented optional portlibs so someone else can clone and `make` successfully.

### Opus (today)

`.opus` / Ogg Opus now routes through **libopusfile** the same way FLAC and MP3 have their own paths — decode-ahead into NDSP, with OpusTags feeding the now-playing panel. Without `3ds-opusfile` installed, the app still builds; Opus paths report unsupported format.

---

## The downs (and how we got out)

### 1. Seek bar / timer / EQ flashing

**Symptom:** Every update flashed. Felt sluggish.

**Wrong first guess:** “We’re redrawing too much.”

**Real cause:** Bottom-screen buffer swapping was showing a half-updated frame.

**Fix:** Single-buffer the bottom screen and compose into an off-screen buffer, then present once. Flicker went away; the UI felt like a UI again.

### 2. Waveform looked “wrong” at 60 FPS

**Symptom:** Bumping the wave to 60 FPS made it look like a scribble racing left-to-right, not music.

**Fix:** Denser sampling and anchoring the trace around zero crossings so it reads as a stable oscilloscope instead of a scrolling strip of noise.

### 3. Visualizer ahead of the speakers

**Symptom:** Bars/wave reacted before you heard the hit.

**Cause:** Analysis ran on decoded PCM before that audio reached NDSP / the DAC.

**Fix:** A short visualization queue so what you see is closer to what you hear.

### 4. WAV stutter after adding the equalizer (the ugly one)

**Symptom:** WAV started stuttering. MP3/FLAC were fine. It showed up right after EQ work, so everything pointed at the visualizer.

**What we tried first:**

- Only compute the active viz mode (bars *or* wave, not both)
- Bigger WAV buffers
- Higher playback thread priority

That helped a little. It didn’t cure it.

**What was actually going on:**

WAV (via vgmstream) was decoding **inline on the playback path**, reading the SD card as it went, with only a few hundred milliseconds of queued audio. MP3 and FLAC already had decode-ahead into a ~2s ring. The EQ/UI work made the main thread busy every frame, so WAV’s fragile design finally showed.

We also found a timing bug: a sleep meant to be ~8 ms was effectively ~8 µs because `svcSleepThread` takes **nanoseconds**. Busy-polling made contention worse.

**Real fix:** Give WAV the same architecture as FLAC — dedicated decode thread + ring buffer — and correct the sleep units. Stutter gone.

Lesson that stuck: the equalizer didn’t “break WAV decoding”; it removed the slack that was hiding a bad I/O design.

### 5. Long filenames failing over FTP

**Symptom:**  
`26. YOU CAN'T GO BACK ~ Apartment Computer & Gospel Computer.mp3` failed. Rename to one character → success. Felt like a hardware limit.

**Cause:** An overly tight path/filename limit in our own normalization code — not the 3DS FAT stack.

**Fix:** Widen path handling. Long names transfer fine.

### 6. “Closed lid still sleeps / audio dies”

**Symptom:** Lid close → silence. We threw APT sleep rejection and NDM locks at it. Still looked broken.

**Plot twist:** The 3DS **mutes the internal speakers when the lid is closed**. Smash Bros. and the official music player need headphones for the same reason.

Once we tested with a headphone jack in, keep-awake + NDM exclusive state worked. We also stop painting frames while the screens are off so the closed-lid loop isn’t wasting cycles on invisible UI.

### 7. Opus “unsupported format -3” after adding support (today)

**Symptom:** Code was in, package installed… still `-3` on device.

**Cause chain:**

1. `dkp-pacman` from PowerShell doesn’t work — portlibs install from the **devkitPro MSYS2** shell (`pacman -S --needed 3ds-opusfile`).
2. After install, the first rebuild failed with `opusfile.h: No such file or directory` because headers live under `include/opus/`, and the Makefile only had `include/`.
3. That left an older binary on the console that truly couldn’t decode Opus.

**Fix:** Add `-I$(PORTLIBS)/include/opus`, rebuild `.3dsx` / `.cia`, copy the new build. Opus plays.

Also documented the MSYS / `pacman` note in the README so the next person (or future us) doesn’t hit the PowerShell trap.

### 8. Delete + shuffle edge case

**Symptom:** Intermittent `prevent playback -7` / unsupported channels `-4` when deleting a file while another track played under shuffle + repeat-all.

**Direction of the fix:** Keep the shuffle bag and playlist indices honest when entries disappear mid-cycle, so the player doesn’t chase a path that no longer exists.

---

## Smaller wins that still mattered

- Konami code easter egg → breadbox toast: `Hi, Phil!` (with A/B remapped to play/pause factored in).
- FTP password alphabet cleaned up for readability on a tiny screen.
- Repo prep: no secrets in tree, third-party notices updated (`stb_image`, etc.), author identity unified, cloneable rebuild with BMP fallbacks when ImageMagick/Pillow aren’t around.
- Coverage gate still at **100%** line/function on the host-tested modules — including new `audio_viz`, `ftp_server`, `trackmeta`, `opus_player`, and friends.

---

## Why this stretch felt worth it

The pattern kept repeating:

1. Ship a feature that makes the player nicer to use.
2. Hit a regression that looks like the new feature’s fault.
3. Dig until the real constraint shows up (display swap, SD latency, speaker mute, include paths).
4. Fix the architecture, not just the symptom.

szmy is in a much better place for daily use now: transfer music without pulling the card, see what’s playing, watch the viz, keep going with the lid closed (headphones in), and play Opus alongside WAV / FLAC / MP3 / vgmstream formats.

If you’re cloning: use the devkitPro MSYS2 shell, install host tools + optional `3ds-opusfile`, then `make` / `make cia`. Details are in the [README](../README.md).

---

*Built with Cursor assistance. Hardware truth still wins every argument.*
