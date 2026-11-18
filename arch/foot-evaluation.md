# foot evaluation

foot is a fast, lightweight Wayland terminal by Daniel Eklöf, in C, under
the MIT licence. It is tam-last's primary reference. Read from source on
2026-11-17, commit `c0d2c3e9` (about 49,000 lines of C).

## How it is built

- **Event loop:** `fdm.c`, a small epoll-based fd multiplexer with
  callbacks; timers are timerfds.
- **Wayland:** libwayland-client, with wayland-scanner output for about
  twenty protocols (`wayland.c`, 2,900 lines).
- **Keys:** xkbcommon for the keymap and compose (`input.c`, 3,700
  lines). Two encoders: the legacy one (`legacy_kbd_protocol`, driven by
  the large tables in `keymap.h`, one row per key and modifier
  combination) and the kitty protocol (`kitty_kbd_protocol`).
- **Parser:** `vt.c` follows Paul Williams' DEC diagram; `csi.c`,
  `osc.c`, `dcs.c` and `sixel.c` dispatch.
- **Screen:** `grid.c` keeps rows in a power-of-two ring with an offset,
  so scrollback and the visible screen are one structure and scrolling
  moves an index, not memory.
- **Fonts:** fcft, Eklöf's own font library over fontconfig, FreeType,
  harfbuzz and utf8proc. `box-drawing.c` (3,450 lines) draws box, block,
  Braille and legacy-computing characters by code so they join exactly.
- **Rendering:** pixman into shm buffers, with worker threads
  (`render_worker_thread`) splitting rows. `shm.c` scrolls by moving the
  buffer's offset within a larger memfd and punching holes
  (`FALLOC_FL_PUNCH_HOLE`) behind it, so a scroll copies nothing.
- **Damage:** per-row dirty flags plus a list of scroll damage, so a
  scroll becomes a pixel move plus the new rows.
- **Server mode:** `server.c` and `client.c`; `footclient` asks a running
  `foot --server` for a new window.

## Ideas worth taking

- **Frame callbacks gate drawing** (adopt, P1 0.0.1). foot renders on
  the frame callback and coalesces everything in between. tam-last does
  the same.
- **The grid as a ring of rows** (adopt, P1 0.0.3). One structure for
  scrollback and screen; scrolling moves an offset.
- **Scroll damage as pixel moves** (adopt, P1 0.0.2).
- **Box drawing by code** (adopt, P1 0.0.4). Fonts rarely make box
  characters join at every size.
- **The kitty keyboard protocol** (adopt, P2 0.1.0); foot's
  implementation is a C reference.
- **Server mode** (adopt, P3 0.2.3).
- **Alt sends ESC by default, with the 8-bit option behind DECSET 1034
  and 1036** (adopt the default now; the modes later).

## Things to avoid

- **Table-driven legacy encoding.** `keymap.h` lists every key and
  modifier combination by hand. The xterm rules are a formula
  (`1 + Shift + 2 Alt + 4 Ctrl + 8 Meta`), and tam-last computes them.
- **The library stack under the fonts.** fontconfig, FreeType, harfbuzz
  and utf8proc bring far more than a monospace terminal needs; that is
  why tam-last has its own rasterizer.
- **Rendering threads by default.** They earn their keep on huge windows,
  but they cost wake-ups. tam-last stays single-threaded until a
  measurement says otherwise.
- **Build-time configuration of everything.** foot has meson options for
  many features; tam-last has one build.

## Where to look

- `vt.c`: the parser and UTF-8 handling.
- `csi.c`: which sequences foot implements, and its replies.
- `input.c`: `keyboard_key`, `legacy_kbd_protocol`, `kitty_kbd_protocol`,
  key repeat (`start_repeater`).
- `grid.c`, `terminal.c`: scrollback, resize and reflow.
- `shm.c`: buffer scrolling with hole punching.
- `doc/foot-ctlseqs.7.scd`: every sequence foot supports, in one list.
