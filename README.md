# 3DS Homebrew – szmy (multi-format audio)

A Nintendo 3DS homebrew music player using **devkitPro**, **libctru**, and **vgmstream**.  
Supports **WAV, FLAC, Opus** (optional), **MP3** (optional), **BRSTM/BCSTM/BFSTM**, ADPCM, and other formats vgmstream can decode with built-in codecs.

## Requirements

- [devkitPro](https://devkitpro.org/) with **devkitARM** and **3DS tools**
- On Windows: use the **devkitPro MSYS2** shell (so `make` and `DEVKITARM` are set).  
  Package installs (`pacman` / `dkp-pacman`) only work from that shell — not from PowerShell or cmd.
- Host toolchain for unit tests / coverage gate: **gcc**, **make**, **lcov**  
  (`pacman -S --needed gcc make lcov` in the same MSYS shell)
- **Windows:** if host tests die with `Permission denied` (shell exit **126**) on
  `tests/build/*.exe`, turn **Smart App Control** **Off**  
  (Windows Security → App & browser control → Smart App Control settings).  
  Evaluation/On blocks unsigned MSYS-built test binaries; regular Defender stays on.

- Optional audio portlibs (same MSYS shell):

  ```bash
  pacman -S --needed 3ds-opusfile   # Opus (.opus / Ogg Opus)
  pacman -S --needed 3ds-mpg123     # optional vgmstream MPEG helper
  ```

  On some setups the wrapper is named `dkp-pacman` instead of `pacman`.

## Build

From a fresh clone (devkitPro MSYS2 shell on Windows):

```bash
git clone https://github.com/czyrustuazon/szmy.git
cd szmy
pacman -S --needed gcc make lcov          # host tests / coverage gate
# optional: pacman -S --needed 3ds-opusfile 3ds-mpg123
make
```

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

## FTP file transfer

Tap the **folder** icon on the bottom screen to start a simple FTP server for the entire SD card (`sdmc:/`).

- Top screen shows `ftp://IP:5000`, username **`szmy`**, and a fresh random 6-character password.
- Playback stops and music controls are disabled while FTP is on; tap the folder icon again to stop and refresh the playlist.
- Uses plain FTP (not FTPS). The password only protects casual LAN access — do not expose the console beyond your trusted network.
- Passive mode (PASV) only; one client at a time.

## Clean

```bash
make clean
```

## Host unit tests

Pure logic and control-plane code is extracted into host-testable modules and exercised with **Unity** + **gcov/lcov** under `tests/`. See **[tests/README.md](tests/README.md)** for the module list, mocks, and how to add coverage.

`make` / `make cia` fail if line or function coverage of those modules drops below 100%. Branch coverage is reported but not gated.

## Audio (vgmstream + FLAC + optional Opus / MP3)

- **vgmstream** is built from `vgmstream/` (`make -C vgmstream -f Makefile.3ds`). Core library only.
- **FLAC** via **dr_flac** (`include/dr_flac.h`) for `.flac` paths.
- **Opus** optional: install `3ds-opusfile` (see Requirements). Routes `.opus` and Ogg files with an `OpusHead` packet. Without the portlib, the app still builds; Opus paths report unsupported format.
- **MP3** optional: install `3ds-mpg123` for vgmstream MPEG. Normal `.mp3` playback uses the in-tree minimp3 path either way.
- Playback uses **NDSP**. Place DSP firmware at `sdmc:/3ds/dspfirm.cdc`.
- Format is detected from content / extension (`.flac` → dr_flac; `.opus` / Ogg Opus → libopusfile; `.mp3` → minimp3; else → vgmstream).

## Project layout

| Path | Role |
|------|------|
| `source/` | App C sources |
| `include/` | Headers |
| `tests/` | Host Unity tests, fixtures, coverage |
| `vgmstream/` | Vendored vgmstream core (3DS build under `vgmstream/build-3ds/`) |
| `gfx/` | Bitmaps / textures (also used as PNG→BMP fallbacks) |
| `data/` | Embedded binary data |
| `Makefile` | 3DS build + coverage gate |
| `szmy.rsf` | CIA Rom Specification |

Build products (`build/`, `*.3dsx` / `*.cia` / `*.elf` / `*.smdh`, `tests/build/`, coverage HTML, gcov data) are gitignored.

PNG sources (`up.png`, `bottom-clean.png`, `generic_music.png`) are converted at build time when ImageMagick or Pillow is available; otherwise the Makefile copies matching `gfx/*.bmp` fallbacks so a clone can still rebuild without those tools.

## New 3DS: why only ~128 MB?

From the **Homebrew Launcher**, apps get the legacy **~128 MB** region. You cannot unlock more from inside the process. For a larger region, build a **CIA**, install it, and launch from the **Home Menu** with New3DS mode set in `szmy.rsf` / exheader (e.g. ~124 MB or ~178 MB). See [NCCH/Extended Header](https://www.3dbrew.org/wiki/NCCH/Extended_Header).

## Customize

- **App name/icon:** `APP_TITLE`, `APP_DESCRIPTION`, `APP_AUTHOR` in the Makefile; optional root `icon.png` (48×48).
- **Target name:** `TARGET` in the Makefile (defaults to the folder name).
