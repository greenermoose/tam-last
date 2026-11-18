# P1: Foundation (0.0.x)

**Status:** in progress. 0.0.1, the skeleton, built on 2026-11-17 and
waiting for Fred to try it. Next: 0.0.2, full-screen programs.

## Goal

A small, clean terminal that runs a shell and the everyday full-screen
programs, on code that is ours from the Wayland socket to the pixels. It
sets the module structure, the limits on untrusted input, the event loop
and the test harness that everything later builds on.

## Versions

### 0.0.1: the skeleton

- Repository, licence and documentation folders, with the reference
  clones in `ideas/sources`.
- Plain Makefile; `make test` and `make sanitize`.
- `src/wl/`: the Wayland wire protocol (socket, framing, strings and
  arrays, fd passing, object ids reused after `delete_id`) and
  hand-written requests for the core and xdg-shell interfaces we use.
- `window.c`: globals, one xdg toplevel, configure and ack, frame
  callbacks, the seat's keyboard, and fractional scaling
  (fractional-scale-v1 with viewporter), brought forward from 0.0.4 when
  Fred found text blurry at scale 1.25 (B1). `shm.c`: two XRGB8888 buffers in POSIX
  shared memory.
- `font/`: TrueType tables (head, hhea, hmtx, maxp, cmap formats 4 and
  12, loca, glyf with composites), an exact-area rasterizer, vertical
  grid fitting to the x-height, and a glyph cache.
- `vt.c` and `term.c`: the DEC parser with UTF-8, and the screen: C0
  controls, cursor movement, ED, EL, ICH, DCH, ECH, IL, DL, SU, SD, SGR
  (16, 256, direct color, colon forms), DSR, DA, DECCKM, DECAWM,
  DECTCEM, DECKPAM, save and restore, wide characters. Strings (OSC, DCS
  and the rest) are skipped.
- `keys/`: our evdev table, keyboard state, the US layout, the legacy
  xterm encoding, and repeat from `repeat_info`.
- `pty.c` and `main.c`: the program on a pty, one poll loop over the
  socket, the pty and a SIGCHLD pipe; the program's exit status becomes
  ours.

### 0.0.2: full-screen programs

- Alternate screen (47, 1047, 1049) with saved cursor.
- Scroll regions (DECSTBM), origin mode, insert mode (IRM), reverse wrap
  is not planned.
- Character sets: DEC special graphics through `ESC ( 0`.
- Combining characters stored with their base cell.
- Cursor styles (DECSCUSR): block, underline, bar; still no blinking
  unless asked.
- Damage tracking: only rows that changed are drawn and damaged, and
  scrolling moves pixels instead of redrawing.

### 0.0.3: scrollback and the pointer

- Scrollback in fixed-size pages with a cap (ghostty's page list and
  foot's ring are the models), wheel and Shift+PgUp/PgDn.
- wl_pointer: cursor shape, selection by drag, word and line selection.
- Clipboard (wl_data_device) and primary selection; paste with bracketed
  paste (2004).
- Mouse reporting (1000, 1002, 1003, 1006).

### 0.0.4: looks

- HiDPI without fractional-scale-v1: wl_surface preferred buffer scale
  (compositor v6), for compositors that offer only integer scales.
- Bold and italic faces from sibling font files; a fallback font for
  characters the main font lacks (CJK, emoji outlines).
- Box-drawing, block and Braille characters drawn by code, so lines join
  exactly (foot's `box-drawing.c` is the model).
- `~/.config/tam-last/tam-last.ini` through `tam_ini`: font, size,
  colors; the default colors follow the Tamlinux theme.

## Acceptance

- `make clean && make`: no warnings with `-Wall -Wextra -Wpedantic
  -Wshadow -Wvla`.
- `make test` and `make sanitize` pass, including the headless run under
  tam-pane.
- `readelf -d build/tam-last` lists only `libc.so.6` and `libm.so.6`.
- Every new escape sequence gets checks in `tests/test_vt.c`; every key
  change gets checks in `tests/test_keys.c`, including the round trip
  through libtam's `tam_key`.
- Fred tries each version. Agents run the exhaustive checks; Fred's
  checks stay short.
- 0.0.1 only: Fred compares tam-last and foot side by side, same font and
  size. If our text looks worse, the FreeType fallback (below) is the
  next step.

## Dependencies

- Uses: libc, libm, libtam (static: `tam_uni` today).
- Added in this phase: none.
- The FreeType fallback, only if the font comparison fails: FreeType
  built with zlib, bzip2, png, brotli and harfbuzz off, recorded in
  `arch/dependencies.md` with a planned exit.

## Risks

- **Text quality without hinting.** Vertical grid fitting covers most of
  what hinting does at terminal sizes; the side-by-side check decides.
- **Wayland edge cases without libwayland.** Every opcode is checked
  against the XML in the tests, and the headless run exercises a real
  compositor. New interfaces get the same two checks.
- **Keys on other layouts.** Only US for now; P2 adds layouts.

## Open points

- TERM: tam-last says `xterm-256color` today. Our own terminfo entry
  arrives in P3 (0.2.1); until then, programs assume xterm.
- Default size: 80 x 24 when the compositor lets us choose.
