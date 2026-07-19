# Host unit tests (szmy)

Desktop-side tests for **pure logic and control-plane code** extracted from the 3DS app.  
Stack: **Unity**, host **gcc**, **gcov/lcov**. Tests run on MSYS2 / Linux / macOS — not on device.

## Commands (from repo root)

| Command | What it does |
|---------|----------------|
| `make` / `make cia` | Runs coverage gate, then builds the 3DS app / CIA |
| `make coverage` | Host tests + **100%** line & function coverage gate (no HTML) |
| `make coverage-html` | Same, then writes `tests/coverage_html/index.html` |
| `make test-host` | Compile and run tests only (no lcov gate) |
| `make SKIP_COVERAGE=1` | Skip the gate for a faster 3DS-only build |

From `tests/`:

```bash
make test          # run all runners
make coverage      # clean rebuild + gate
make coverage-html # gate + HTML report
```

Requires: `gcc`, `make`, `lcov` (e.g. `pacman -S --needed gcc make lcov` in the devkitPro MSYS shell).

Parallel jobs are on by default (`nproc`). Override with `make -j4` or `make JOBS=8`.

## What is covered

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
| Music browser logic | `source/musiclist.c` | Real temp directories (`mkdtemp`) |
| EQ / scope viz | `source/audio_viz.c` | Synthetic PCM sine/square windows |
| FTP path/auth helpers | `source/ftp_server.c` | String / auth unit tests (socket loop is 3DS-only) |
| MP3 player | `source/mp3_player.c` | `fixtures/mini.mp3` + **host NDSP/thread mocks** |
| FLAC player | `source/flac_player.c` | **Host dr_flac stub** + NDSP/thread mocks |
| Opus player | `source/opus_player.c` | **Host libopusfile stub** + NDSP/thread mocks |

**~180+ tests** across **15 runners**. The coverage gate requires **100% line and function** coverage on these production sources (branch coverage is reported, not gated). Test / Unity / minimp3 paths are stripped from the lcov report.

## What is *not* covered (and why)

| File | Reason |
|------|--------|
| `main.c` | Full app entry, APT loop |
| `audio.c` (vgmstream playback) | NDSP + vgmstream + threads (not host-mocked yet) |
| `botbuttons.c` (frame loop) | Touch/GFX — BMP helpers live in `bmp_util.c` |
| `jptext.c`, `topfont.c`, `topbg.c` | GPU / citro3d |

Host tests do **not** replace on-device checks for DSP timing, GPU, or real SDMC behaviour.

## Host mocks (`tests/support/`)

| File | Role |
|------|------|
| `3ds.h` | Stub libctru types (Thread, NDSP, `linearAlloc`, …) |
| `host_mocks.c` | pthread threads, malloc alloc, **PCM capture sink** |
| `host_drflac.c` | Fake `drflac_*` for FLAC player control-flow tests |
| `host_opusfile.c` / `host_opusfile.h` | Fake `op_*` for Opus player control-flow tests |
| `dirent_compat.h` | `DT_*` helpers for directory scanning tests |

MP3 tests use **real minimp3** decode on `fixtures/mini.mp3`. FLAC and Opus tests use stub decoders so playback logic is exercised without binary fixtures.

## Are mocked tests valid?

| Approach | Valid for | Not valid for |
|----------|-----------|---------------|
| **Extract pure functions** (preferred) | Logic, parsing, state machines | Whether NDSP actually outputs sound |
| **Real OS services** (temp dirs, files) | `musiclist` scan/sort/nav, magic reads | Exact SDMC paths on device |
| **Stub platform APIs** | *Your* code paths that call them | libctru / DSP firmware behaviour |

**Rule of thumb:** tests prove branching and data handling in *your* code. Prefer extraction over mocks; use real files/dirs when possible.

## Layout

```
tests/
  Makefile              host gcc, -DUNIT_TEST, --coverage, coverage gate
  test_*.c              Unity runners
  fixtures/             tiny media used by player / magic tests
  support/              3ds.h stubs, host_mocks, host_drflac
  third_party/unity/    Unity framework
  scripts/coverage.sh   optional: coverage-html + open report
  build/                (gitignored) binaries + .gcda/.gcno
  coverage_html/        (gitignored) optional HTML report
```

The tests Makefile **forces a native host `gcc`**, even when a parent `make cia` has exported `arm-none-eabi-gcc` as `CC`.

## Adding coverage

1. Move testable logic out of 3DS-tied files into `source/*.c` + `include/*.h`.
2. Add `tests/test_<name>.c` and list the runner in `TEST_RUNNERS` in `tests/Makefile`.
3. Prefer extraction over new `UNIT_TEST` inject hooks; assert behaviour, not just “touch this line.”
4. Run `make coverage` from the repo root and keep the gate green.
