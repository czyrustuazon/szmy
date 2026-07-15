# Host unit tests (szmy)

Desktop-side tests for **pure logic and control-plane code** extracted from the 3DS app.

## What is covered (8 source files)

| Module | File | How tested |
|--------|------|------------|
| ID3 skip | `source/id3_util.c` | Synthetic byte buffers |
| Paths / labels | `source/path_util.c` | String inputs |
| Error strings | `source/audio_errors.c` | Error codes |
| BMP parsing / hit tests | `source/bmp_util.c` | Embedded minimal BMP bytes |
| Font init errors | `source/jptext_errors.c` | Error codes |
| Pause/stop/resume state | `source/audio_ctrl.c` | Direct API calls (no NDSP) |
| FLAC magic / route pick | `source/file_magic.c` | Real temp files |
| PCM ring buffer | `source/pcm_ring.c` | Synthetic byte patterns (wrap, partial drain) |
| MP3 player | `source/mp3_player.c` | Real `fixtures/mini.mp3` + **host NDSP/thread mocks** |
| FLAC player | `source/flac_player.c` | **Host dr_flac stub** + NDSP/thread mocks |
| Music browser logic | `source/musiclist.c` | **Real temp directories** (`mkdtemp`) |

**60 tests** across 11 runners. Run `make coverage` → `coverage_html/index.html`.

## What is *not* covered (and why)

| File | Reason |
|------|--------|
| `main.c` | Full app entry, APT loop |
| `audio.c` (playback loop) | NDSP + vgmstream + threads (not yet host-mocked) |
| `botbuttons.c` (frame loop) | Touch/GFX — BMP helpers in `bmp_util.c` |
| `jptext.c`, `topfont.c`, `topbg.c` | GPU / citro3d |

## Host mocks (`tests/support/`)

| File | Role |
|------|------|
| `3ds.h` | Stub libctru types (Thread, NDSP, linearAlloc, …) |
| `host_mocks.c` | pthread threads, malloc alloc, **PCM capture sink** (counts samples in `ndspChnWaveBufAdd`) |
| `host_drflac.c` | Fake `drflac_*` for FLAC player tests (`HOST_FLAC_FIXTURE` path) |

MP3 tests use **real minimp3** decode on `fixtures/mini.mp3`. FLAC tests use the dr_flac stub (not the full `dr_flac.h` implementation) so playback logic is exercised without a binary FLAC fixture.

## Are mocked tests valid?

**It depends what you mock.**

| Approach | Valid for | Not valid for |
|----------|-----------|---------------|
| **Extract pure functions** (preferred) | Logic, parsing, state machines | Whether NDSP actually outputs sound |
| **Real OS services** (temp dirs, files) | `musiclist` scan/sort/nav, FLAC magic read | SDMC paths on device |
| **Stub platform APIs** (fake `threadCreate`, NDSP) | *Your* code paths that call them | libctru / DSP firmware behaviour |
| **Full hardware mock** | Rarely worth it on 3DS homebrew | End-to-end audio/video |

**Rule of thumb:** tests prove **your branching and data handling** are correct. They do **not** replace on-device testing for timing, DSP, and GPU.

Most tests here use **no mocks** — only extracted C and the real host filesystem.

## Quick start

```bash
pacman -S --needed gcc make lcov   # devkitPro MSYS shell
cd /c/Users/Zepse/Documents/szmy/tests
make test
make coverage
```

See earlier sections in git history for Ubuntu/macOS/full MSYS2 notes.

## Layout

```
tests/
  Makefile              -DUNIT_TEST, --coverage
  test_*.c
  support/3ds.h         stub (unused when musiclist skips draw)
  support/dirent_compat.h
  third_party/unity/
```

## Adding coverage

1. Move testable logic out of 3DS-tied files into `source/*.c`.
2. Add `tests/test_<name>.c` and list it in `TEST_RUNNERS`.
3. Prefer extraction over mocks; use real files/dirs when possible.
