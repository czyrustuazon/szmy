# 3DS Homebrew – szmy (multi-format audio)

A Nintendo 3DS homebrew app using **devkitPro**, **libctru**, and **vgmstream** for multi-format audio playback.  
Supports **WAV, BRSTM, ADPCM, and many other formats** via vgmstream (no optional codec libs; built-in decoders only).  
Press **A** to play `sdmc:/music.wav` (or any supported file at that path); **START** to exit.

## Requirements

- [devkitPro](https://devkitpro.org/) with **devkitARM** and **3DS tools** installed
- On Windows: use the devkitPro shell (or ensure `make` and `DEVKITARM` are in your PATH)

## Build

1. Set the environment (if not already set by the devkitPro shell):
   - **Windows:** `set DEVKITARM=C:\devkitPro\devkitARM`
   - **Linux/macOS:** `export DEVKITARM=/opt/devkitPro/devkitARM`

2. From the project root, run:
   ```bash
   make
   ```

3. Output (names match the project folder / `TARGET`, e.g. **szmy**):
   - `szmy.3dsx` – homebrew executable (run via 3DS homebrew launcher)
   - `szmy.smdh` – icon/metadata
   - `szmy.elf` – ELF for debugging

## Build CIA (installable, more RAM on New 3DS)

To build an installable `.cia` (e.g. for **~124 MB** RAM on New 3DS when launched from the Home Menu):

1. Ensure **makerom** is in your PATH. It is **not** in devkitPro pacman. Get it by:
   - **Prebuilt (Windows):** Download from [Project_CTR releases](https://github.com/3DSGuy/Project_CTR/releases) (look for makerom in the release assets) or from [GBAtemp](https://gbatemp.net/download/project_ctr-ctrtool-makerom-win64-source.35067/) (Win64 build). Put `makerom.exe` in a folder that’s on your PATH (e.g. `C:\devkitPro\tools\bin`) or run `make cia` from the same folder.
   - **Build from source:** Clone [3DSGuy/Project_CTR](https://github.com/3DSGuy/Project_CTR), open the `makerom` folder, and build with make (see the repo’s README). Requires a C++ build environment (e.g. MSYS2 with gcc, or Visual Studio).
2. Run:
   ```bash
   make cia
   ```
3. You get `szmy.cia` (if the project folder is `szmy`). Install it with FBI or similar, then **launch from the Home Menu** (not from the homebrew launcher) to use the extended memory.

Add **`szmy.rsf`** in the project root (Rom Specification) with **New3DS SystemModeExt: 124MB** (or your choice). For **178 MB** instead, edit the RSF and set `SystemModeExt : 178MB`.

## Run on 3DS

- Copy `szmy.3dsx` (and optionally `szmy.smdh` in the same folder) to your SD card, e.g. in `sd:/3ds/`.
- Launch from the homebrew launcher (e.g. Luma3DS + Homebrew Menu).

## Clean

```bash
make clean
```

## Audio (vgmstream + FLAC + optional MP3)

- **vgmstream** is built from `vgmstream-master/` (3DS build: `make -C vgmstream-master -f Makefile.3ds`). Plugin/player code (winamp, xmplay, fb2k, audacious, cli) has been removed; only the core library is used.
- **FLAC** is supported via **dr_flac** (single-file decoder in `include/dr_flac.h`). Files whose path ends with `.flac` are decoded and played without vgmstream.
- **MP3** is optional. Install libmpg123 for 3DS so `.mp3` files work: `dkp-pacman -S 3ds-mpg123` (or ensure `$(DEVKITPRO)/portlibs/3ds/lib/libmpg123.a` exists). Without it, the app still builds and plays WAV/FLAC/BRSTM/etc.
- Playback uses **NDSP**. You need DSP firmware on the SD: place `dspfirm.cdc` at `sdmc:/3ds/dspfirm.cdc` (dump from a real 3DS or use a provided file from the community).
- **File name/path:** Any path is fine (e.g. `sdmc:/music.wav`, `sdmc:/track.flac`, `sdmc:/song.mp3`). Format is detected from content or extension (`.flac` uses dr_flac; others use vgmstream).
- **Supported formats:** WAV, FLAC, BRSTM, BCSTM, BFSTM, many ADPCM variants, and (if libmpg123 is installed) MP3, plus other formats vgmstream supports with built-in codecs.

## Project layout

- `source/` – C/C++ source (`main.c`, `audio.c`, `flac_player.c`)
- `include/` – project headers (`audio.h`, `dr_flac.h`)
- `vgmstream-master/` – vgmstream library (core only; plugins removed)
- `data/` – binary data (included in the build)
- `gfx/` – graphics (e.g. `.t3s` textures)
- `Makefile` – devkitPro 3DS build rules (builds vgmstream then the app)
- `szmy.rsf` – Rom Specification File for `make cia` (add yourself; New3DS extended memory when configured there)

## New 3DS: why only ~128 MB?

On New 3DS XL the system has **256 MB** RAM, but when you run homebrew from the **Homebrew Launcher** (hbmenu), your app only gets **~128 MB**. That’s not a bug:

- The launcher runs in **Legacy** mode (Old 3DS layout). The kernel only gives your process the old 128 MB linear region; the other 128 MB is reserved for the system or not mapped for your process.
- **You cannot change this from inside your app.** The memory layout is fixed when the process is created; there is no API to “unlock” more RAM at runtime.
- Even in the best case (see below), **apps never get the full 256 MB**. The rest is used by the OS and services.

### Getting more than 128 MB (optional)

To get a **larger** app memory region (still not 256 MB):

1. **Build a CIA** and install it, then **launch from the Home Menu** (not from hbmenu). The system then uses your title’s **exheader** to decide memory mode.
2. In the exheader, set **New3DS system mode** (Flag2) to request more RAM, e.g.:
   - **Prod (1):** ~124 MB for your app  
   - **Dev1 (2):** ~178 MB for your app  
   See [NCCH/Extended Header](https://www.3dbrew.org/wiki/NCCH/Extended_Header) (Flag2, New3DS system mode).
The **`make cia`** target uses `szmy.rsf` (i.e. `$(TARGET).rsf`) and New3DS settings from that file (see Build CIA above).

So: **from 3dsx launched by hbmenu you cannot “force” access to the rest of the 256 MB**; the only way to get more is to run as a CIA from the Home Menu with an exheader that requests a larger New3DS memory mode.

## Customize

- **App name/icon:** Edit `APP_TITLE`, `APP_DESCRIPTION`, `APP_AUTHOR` in the Makefile. Add `icon.png` (48×48) in the project root for a custom icon.
- **Target name:** Change `TARGET` in the Makefile (default is the folder name).
