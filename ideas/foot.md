# foot

## What it is

A fast, lightweight Wayland terminal in C by Daniel Eklöf; MIT. Uses
libwayland, xkbcommon, pixman, and fcft (fontconfig, FreeType, harfbuzz,
utf8proc). About 49,000 lines. Our primary reference; read from source.
The full evaluation is `arch/foot-evaluation.md`.

## Ideas worth taking

- **Draw on the frame callback** — adopt (P1, 0.0.1). Coalesces output
  bursts into one frame and costs nothing when idle.
- **Rows in a power-of-two ring** — adopt (P1, 0.0.4). Scrollback and
  screen are one structure.
- **Scroll by moving the buffer offset in a memfd and punching holes**
  — consider (P1, 0.0.3). Zero-copy scrolling; Linux-specific
  (`fallocate`), so it needs a portable fallback for musl and the BSDs.
- **Box drawing by code** — adopt (P1, 0.0.5).
- **Server mode with a thin client** — adopt (P3, 0.2.3).
- **`foot-ctlseqs(7)`, one manual page listing every sequence** — adopt
  (P3, 0.2.1), as our own `tam-last-ctlseqs`.

## Things to avoid

- Hand-written tables for every legacy key and modifier combination
  (`keymap.h`); a formula does it.
- Worker threads for rendering by default.

## Where to look

`vt.c`, `csi.c`, `input.c`, `grid.c`, `shm.c`, `render.c`,
`box-drawing.c`, `doc/foot-ctlseqs.7.scd`.
