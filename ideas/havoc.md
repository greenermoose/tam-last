# havoc

## What it is

A minimal Wayland terminal in C by ii8; public domain or MIT. About
4,000 lines in two files (`main.c`, `glyph.c`). Uses libwayland,
xkbcommon and libtsm (carried in `tsm/`). Read from source.

## Ideas worth taking

- **No FreeType: a modified stb_truetype draws the glyphs** (`glyph.c`)
  — adopt (P1, 0.0.1). The closest precedent for tam-last's own
  rasterizer, and proof that a terminal looks fine without FreeType.
- **A TrueType font compiled into the binary as a fallback**
  (`fallback.h`, about 15 KiB of font) — consider (P1, 0.0.5). tam-last
  would always start, even with no fonts installed. Needs a small
  monospace font under a licence that allows it (the OFL does).
- **A font is a path in the configuration, not a fontconfig pattern**
  — adopt (P1, 0.0.1).
- **One small configuration file with sections** — adopt (P1, 0.0.5),
  through `tam_ini`.

## Things to avoid

- **Delegating the screen to libtsm.** It saves code, but the parser and
  screen are the heart of a terminal, and we want them in our hands.

## Where to look

`glyph.c` (stb_truetype trimmed for a terminal), `main.c` (the whole
Wayland client in one file), `havoc.cfg`.
