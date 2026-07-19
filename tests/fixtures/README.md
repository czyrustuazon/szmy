# Test fixtures

- `empty.mp3` — 0-byte file (error-path tests).
- `garbage.mp3` — 10 bytes of non-audio junk (magic / reject paths).
- `mini.mp3` — first ~32 KiB of a SoundHelix example song (enough for
  decode/playback tests). Free for any use with credit to **SoundHelix** and
  **T. Schürger** (https://www.soundhelix.com/audio-examples).

Regenerate a similar clip:

```bash
head -c 32768 /path/to/any.mp3 > tests/fixtures/mini.mp3
```
