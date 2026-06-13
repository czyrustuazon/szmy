# Third-party notices

This document lists libraries and tools used by **szmy** (3DS homebrew music
player). It is for attribution and license compliance. Full license texts are
in the referenced files or upstream repositories.

## szmy application code

Copyright (c) 2026 zaccken. Licensed under the zlib License. See `LICENSE`.

---

## Runtime libraries (linked into szmy.3dsx / szmy.cia)

### libctru

- **Use:** 3DS OS services, graphics, NDSP, threading, filesystem, etc.
- **License:** zlib License
- **Source:** https://github.com/devkitPro/libctru

### citro3d

- **Use:** GPU rendering (citro2d backend, background texture)
- **License:** zlib License
- **Source:** https://github.com/devkitPro/citro3d

### citro2d

- **Use:** 2D text (system font / CJK), sprites, screen targets
- **License:** zlib License
- **Source:** https://github.com/devkitPro/citro2d

### vgmstream (libvgmstream)

- **Use:** Decoding game and common audio formats (WAV, BRSTM, OGG, etc.)
- **License:** ISC-style permissive license (see `vgmstream-master/COPYING`)
- **Source:** https://github.com/vgmstream/vgmstream (vendored as `vgmstream-master/`)

  vgmstream incorporates code under other terms. Notable portions credited in
  `vgmstream-master/COPYING` include work by Marko Kreen, jagarl, Nullsoft,
  Paul Hsieh, Leshade Entis, and Sun Microsystems (public domain).

### minimp3 (via vgmstream)

- **Use:** MP3 decode path (symbols linked from vgmstream’s minimp3 build)
- **License:** CC0 1.0 Universal (public domain dedication)
- **Source:** https://github.com/lieff/minimp3
- **In tree:** `vgmstream-master/src/coding/libs/minimp3.h`

### dr_flac

- **Use:** FLAC playback (`include/dr_flac.h`, `source/flac_player.c`)
- **License:** Your choice of **The Unlicense** (public domain) or **MIT-0**
- **Source:** https://github.com/mackron/dr_libs (dr_flac)
- **In tree:** `include/dr_flac.h` (license text at end of file)

### libmpg123 (optional)

- **Use:** Only if `libmpg123.a` is present when building; enables vgmstream
  MPEG decode via `ENABLE_MP3=1`. **Not required** for normal `.mp3` files in
  current builds (minimp3 path in `source/mp3_player.c`).
- **License:** LGPL 2.1 (typical for mpg123; verify your portlibs package)
- **Package:** devkitPro `3ds-mpg123` portlib

### Newlib / compiler runtime

- **Use:** C standard library and ARM EABI support objects linked by devkitARM
- **License:** Mixed; newlib is under various licenses (see newlib and GCC
  runtime exception documentation). You generally do not need to ship newlib
  source when distributing a homebrew binary.

---

## Build-time tools (not linked into the app)

### devkitARM (GCC, binutils, etc.)

- **Use:** Cross-compiler and linker
- **License:** GPL (compiler); see GCC runtime library exception for linked
  runtime components
- **Source:** https://github.com/devkitPro/buildscripts / devkitPro pacman

### tex3ds

- **Use:** Converts `.t3s` / embed BMP to `.t3x` GPU textures (`Makefile`)
- **License:** zlib License (citro3d / devkitPro ecosystem)
- **Source:** https://github.com/devkitPro/citro3d (tex3ds tool)

### makerom

- **Use:** Builds `.cia` when you run `make cia`
- **License:** See devkitPro / makerom upstream (GPL applies to the tool itself;
  not embedded in the homebrew binary)

---

## devkitPro trademark notice

devkitPro, devkitARM, devkitPPC, and related names and logos are trademarks of
Dave Murphy. See:

https://devkitpro.org/wiki/Trademarks

Using devkitPro libraries does not grant rights to use devkitPro trademarks in
your product name or marketing without separate permission.

---

## Suggested attribution (optional)

If you distribute szmy binaries, you may include a short notice such as:

> szmy — Copyright (c) 2026 zaccken (zlib License).
> Uses libctru, citro2d, citro3d, vgmstream, dr_flac, and minimp3.
> See THIRD_PARTY_NOTICES.md for details.

---

## Updating this file

When you add dependencies, append a section with name, use, license, and
source URL. If you change the license of szmy’s own code, update `LICENSE` as
well.
