# 3DS Homebrew – szmy (multi-format audio)

A Nintendo 3DS homebrew music player using **devkitPro**, **libctru**, and **vgmstream**.  
Supports **WAV, FLAC, MP3** (optional), **BRSTM/BCSTM/BFSTM**, ADPCM, and other formats vgmstream can decode with built-in codecs.

## Requirements

- [devkitPro](https://devkitpro.org/) with **devkitARM** and **3DS tools**
- On Windows: use the **devkitPro MSYS2** shell (so `make` and `DEVKITARM` are set)
- Host toolchain for unit tests / coverage gate: **gcc**, **make**, **lcov**  
  (`pacman -S --needed gcc make lcov` in the same MSYS shell)

## Build

1. Set the environment if needed:
   - **Windows (MSYS):** usually already set by the devkitPro shell  
   - **Linux/macOS:** `export DEVKITARM=/opt/devkitPro/devkitARM`

2. From the project root:

   ```bash
   make
   ```

   By default this:

   1. Runs the **host unit tests + coverage gate** (must be **100%** line and function coverage on tested production sources)
   2. Builds the 3DS app in parallel (`-j` defaults to all CPU cores)

3. Outputs (names follow the project folder / `TARGET`, e.g. **szmy**):

   - `szmy.3dsx` – homebrew launcher executable
   - `szmy.smdh` – icon / metadata
   - `szmy.elf` – ELF for debugging

### Useful overrides

| Command | Effect |
|---------|--------|
| `make SKIP_COVERAGE=1` | Skip host tests; 3DS build only (faster local iterate) |
| `make -j4` / `make JOBS=8` | Cap parallel jobs |
| `make test-host` | Run host tests only (no coverage report / gate) |
| `make coverage` | Host tests + coverage gate (no HTML) |
| `make coverage-html` | Same as coverage, then write `tests/coverage_html/index.html` |

## Build CIA (installable, more RAM on New 3DS)

Installable `.cia` for launching from the **Home Menu** (larger New 3DS memory mode):

1. **makerom** on `PATH` (not in default devkitPro pacman). Get a build from [Project_CTR releases](https://github.com/3DSGuy/Project_CTR/releases) or build from source.
2. **bannertool** for `banner.bin` (see Makefile comments / [3ds-bannertool releases](https://github.com/carstene1ns/3ds-bannertool/releases)).
3. Root **`szmy.rsf`** with your New3DS `SystemModeExt` (e.g. `124MB` or `178MB`).

```bash
make cia
```

Same coverage gate as `make` unless you pass `SKIP_COVERAGE=1`. Install with FBI (or similar), then **launch from the Home Menu**.

## Run on 3DS

- Copy `szmy.3dsx` (and optionally `szmy.smdh`) to the SD card, e.g. `sd:/3ds/`.
- Launch from the homebrew launcher (e.g. Luma3DS + Homebrew Menu).

## Clean

```bash
make clean
```

## Host unit tests

Pure logic and control-plane code is extracted into host-testable modules and exercised with **Unity** + **gcov/lcov** under `tests/`. See **[tests/README.md](tests/README.md)** for the module list, mocks, and how to add coverage.

`make` / `make cia` fail if line or function coverage of those modules drops below 100%. Branch coverage is reported but not gated.

## Audio (vgmstream + FLAC + optional MP3)

- **vgmstream** is built from `vgmstream-master/` (`make -C vgmstream-master -f Makefile.3ds`). Core library only.
- **FLAC** via **dr_flac** (`include/dr_flac.h`) for `.flac` paths.
- **MP3** optional: `dkp-pacman -S 3ds-mpg123` (needs `$(DEVKITPRO)/portlibs/3ds/lib/libmpg123.a`). Without it, the app still builds for other formats.
- Playback uses **NDSP**. Place DSP firmware at `sdmc:/3ds/dspfirm.cdc`.
- Format is detected from content / extension (`.flac` → dr_flac; others → vgmstream / MP3 path as configured).

## Project layout

| Path | Role |
|------|------|
| `source/` | App C sources |
| `include/` | Headers |
| `tests/` | Host Unity tests, fixtures, coverage |
| `vgmstream-master/` | vgmstream core (3DS build under `build-3ds/`) |
| `gfx/` | Bitmaps / textures |
| `data/` | Embedded binary data |
| `Makefile` | 3DS build + coverage gate |
| `szmy.rsf` | CIA Rom Specification |

Build products (`build/`, `*.3dsx` / `*.cia` / `*.elf` / `*.smdh`, `tests/build/`, coverage HTML, gcov data) are gitignored.

## New 3DS: why only ~128 MB?

From the **Homebrew Launcher**, apps get the legacy **~128 MB** region. You cannot unlock more from inside the process. For a larger region, build a **CIA**, install it, and launch from the **Home Menu** with New3DS mode set in `szmy.rsf` / exheader (e.g. ~124 MB or ~178 MB). See [NCCH/Extended Header](https://www.3dbrew.org/wiki/NCCH/Extended_Header).

## Customize

- **App name/icon:** `APP_TITLE`, `APP_DESCRIPTION`, `APP_AUTHOR` in the Makefile; optional root `icon.png` (48×48).
- **Target name:** `TARGET` in the Makefile (defaults to the folder name).
