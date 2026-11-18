# Code taken from other projects

tam-last is written fresh. When another terminal has code that works
well, we read it, then write it into tam-last's own modules in C and in
our own style. This file lists every such piece: where it came from, what
changed, and why it earned its place. Ideas taken without code go in
`ideas/` instead.

When you take something new, add a row here in the same change, and add
the project's notice to `COPYRIGHT`.

Licences matter when taking code. tam-last is GPL-3.0-or-later. Code under
a permissive licence can come in with its notice kept: foot (MIT), st
(MIT), havoc (public domain or MIT), ghostty (MIT), wezterm (MIT),
alacritty (Apache 2.0 or MIT), libxkbcommon (MIT and similar; check each
file), stb (public domain or MIT) and libwayland (MIT). kitty is GPL v3,
which matches. The Linux kernel's console code is GPL v2 only, and GPL
v2-only code cannot be combined with GPL v3 code, so from it we take ideas
only, never code. Always check the file's own header as well.

**Reference clones.** These are kept outside this repository and are
updated with `tools/pull-references` (see `ideas/sources`). The commits
below are the ones read on 2026-11-17.

- foot: `~/Code/foot`, `https://codeberg.org/dnkl/foot.git`, commit
  `c0d2c3e9`. The primary reference.
- havoc: `~/Code/havoc`, commit `ae9d676`.
- st: `~/Code/st`, commit `fdaadd5`.
- kitty: `~/Code/kitty`, commit `a1085a7`.
- alacritty: `~/Code/alacritty`, commit `aa5d942`.
- ghostty: `~/Code/ghostty`, commit `47b5c20`.
- wezterm: `~/Code/wezterm`, commit `e1eae81`.
- libxkbcommon: `~/Code/libxkbcommon`, commit `15b2b4e`.
- stb: `~/Code/stb`, commit `9600522`.
- wayland: `~/Code/wayland`, commit `f2ecaa6`.

## Pieces taken

None yet.

## Carried unchanged

- `protocols/wayland.xml` (wayland 1.24, as installed on Arch on
  2026-11-17) and `protocols/xdg-shell.xml` (wayland-protocols, stable).
  Our hand-written requests in `src/wl/proto.c` follow them, and
  `tests/test_wire.c` checks every opcode we use against them.
- `protocols/viewporter.xml` (wayland-protocols, stable) and
  `protocols/fractional-scale-v1.xml` (wayland-protocols, staging), for
  sharp text on scaled outputs (B1).

## Compared with, not taken

- The parser state machine is Paul Williams' published DEC diagram
  (vt100.net), as foot's `vt.c` follows it; `src/vt.c` implements it
  fresh.
- The rasterizer (`src/font/raster.c`) uses the exact-area accumulation
  method of Raph Levien's font-rs, also used by stb_truetype's version 2
  rasterizer. It was written from the description of the method, then
  checked against `stb_truetype.h`.
- The legacy key encoding was written from xterm's ctlseqs and checked
  against foot's `input.c` (`legacy_kbd_protocol`) and `keymap.h`.
- The wire framing and fd passing were written from the Wayland protocol
  documentation and checked against libwayland's `src/connection.c`.
