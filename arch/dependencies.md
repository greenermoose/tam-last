# Dependency ledger

Every library tam-last uses is listed here, with why it is needed and
which version removes it. The goal is the C library and libtam, and
nothing else (`arch/principles.md`). Update this file in the same change
as any dependency change.

## Direct dependencies (0.0.1)

`readelf -d build/tam-last` lists only `libc.so.6` and `libm.so.6` as
NEEDED.

- **The C library** (glibc on Arch; musl must work too)
  - **Why:** system calls and standard C. We use X/Open 7 interfaces
    (`-D_XOPEN_SOURCE=700`: `posix_openpt`, `shm_open`), plus
    `_DEFAULT_SOURCE` in `src/wl/conn.c` alone for the `CMSG_*` macros.
  - **Planned exit:** none; it is the goal.
- **libm** (the C library's maths half)
  - **Why:** `sqrtf`, `floorf`, `ceilf`, `roundf` in the rasterizer and
    font metrics.
  - **Planned exit:** none; it is part of standard C.
- **libtam** (static, `$(LIBTAM_PREFIX)/lib/libtam.a`)
  - **Why:** `tam_uni`: UTF-8 decoding and character widths. The tests
    also use `tam_key` for the round trip. From 0.0.4, `tam_ini` for the
    configuration file and `tam_path` for its location.
  - **Planned exit:** none; it is the goal.

## Build-time only

- **make and a C11 compiler.** Plain Make; no other build system.
- **ASan and UBSan** from the compiler, for `make sanitize`.
- **A POSIX sh** runs `tests/headless.sh`, and **tam-pane** (optional)
  is the compositor it runs under.
- **Kernel headers** (optional): `tests/test_keys.c` checks our evdev
  table against `linux/input-event-codes.h` when it is installed.

## Run time, not linked

- **A Wayland compositor** with `wl_compositor` version 4 or later,
  `wl_shm`, `xdg_wm_base` and a seat.
- **A TrueType font** with glyf outlines.

## What other terminals use that we do not

- **libwayland-client and wayland-scanner:** our own wire code
  (`src/wl/`) and hand-written requests, checked against the XML.
- **xkbcommon:** our own evdev table and layout (`src/keys/`). The
  compositor's XKB keymap is received and closed unread.
- **FreeType, fontconfig, harfbuzz** (foot through fcft, and almost every
  other terminal): our own TrueType parser and rasterizer, and a short
  list of font paths. Fallback decided by Fred on 2026-11-17: if our text
  looks worse than foot's side by side, FreeType comes in, built with
  zlib, bzip2, png, brotli and harfbuzz off, and listed here with a
  planned exit.
- **pixman** (foot): our own fills and alpha blending.
- **utf8proc** (foot, for grapheme clusters): libtam's `tam_uni` and the
  C library's `wcwidth` for now; grapheme clusters are an open point.
- **libtsm** (havoc): our own parser and screen.
- **tllist** (foot): plain arrays.
- **meson and ninja.**
