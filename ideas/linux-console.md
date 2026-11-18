# The Linux console

## What it is

The kernel's own terminal on text VTs (`drivers/tty/vt/`), GPL v2 only:
ideas only, never code. Starter note. Fred uses the kernel console on
text VTs, so it is tam-last's sibling on the same machine.

## Ideas worth taking

- **The keymap as a table from key code to action, per modifier
  combination** (`keyboard.c`, `defkeymap.c`, `loadkeys`) — adopt (P2,
  0.1.2). The kernel has mapped evdev codes to characters without
  xkbcommon for thirty years; tam-last's compiled layouts follow the same
  shape.
- **Console fonts (PSF) and the glyph look-alikes `fred-console-glyphs`
  loads** — consider. A bitmap fallback for when no TrueType font is
  installed.

## Things to avoid

- **The console's limited Unicode and colour support.** 512 glyphs, 16
  colours.

## Where to look

`drivers/tty/vt/keyboard.c`, `drivers/tty/n_tty.c` (the line discipline
of `arch/key-path.md`, hop 5), `console_codes(4)`.
