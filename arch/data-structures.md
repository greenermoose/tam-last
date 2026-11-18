# Data structures

What tam-last 0.0.1 keeps in memory, module by module, and what comes
next.

## The application (`src/main.c`)

`struct app` holds one of each: the window, the screen, the font, the pty,
the keyboard state, the palette, focus, and key-repeat state. Sizes come
in two units: the window's size in logical pixels, which the compositor
sets, and the buffer's size in device pixels, which is the logical size
times the output's scale (in 120ths, from fractional-scale-v1). The font
is opened at 96 dpi times that scale, so cells, padding and glyphs are
all in device pixels, and the viewport shows the buffer at the logical
size. There is one window per process until the server mode (P3).

## The Wayland connection (`include/tam-last/wire.h`)

- `struct tl_wire`: the socket, a 16 KiB input buffer with a read offset
  (messages are decoded in place and the buffer is compacted only before
  the next read), a 16 KiB output buffer, queues of 28 received and 28
  outgoing file descriptors, and the object table.
- The object table is an array of 1024 `{interface, data}` entries
  indexed by object id. Ids are allocated lowest-free-first and reused
  after the server's `delete_id`, as libwayland does, so they stay small
  and the compositor's own table stays small too. An object we destroyed
  is a zombie until `delete_id`; a released keyboard keeps its interface
  so a keymap still in flight has its descriptor taken from the queue.
- `struct tl_msg` builds one request (up to 4096 bytes, libwayland's
  limit); `struct tl_event` reads one event, with a `bad` flag set by any
  read past the end or any malformed string.

## The window (`include/tam-last/window.h`, `shm.h`)

Ids of the globals and our objects (including the viewport and the
fractional-scale object, when the compositor offers them, and the
viewport destination last set), the pending toplevel size, the frame
callback id (non-zero while a frame is in flight), and two
`struct tl_buffer`: a shared-memory XRGB8888 image, its `wl_buffer` id,
and a busy flag cleared by `wl_buffer.release`. A buffer of the wrong
size is replaced when it is next needed.

## The screen (`include/tam-last/term.h`)

- `struct tl_cell`: code point, foreground, background (each a 32-bit
  color: default, palette index, or direct RGB in the low 24 bits), and
  16 bits of attributes. 16 bytes per cell; an 80 x 24 screen is 30 KiB.
- The grid is one array of `cols * rows` cells, row by row. Scrolling is
  a `memmove`. The right half of a wide character is a cell holding
  `TL_CELL_WIDE_TAIL`.
- The cursor, the pending-wrap flag (the last column was written; wrap
  before the next character), the pen, the saved cursor, the modes, a
  dirty flag, and a 256-byte buffer of replies to queries.
- `struct tl_vt`: the parser state, up to 32 parameters (each capped at
  65535, with a flag for colon sub-parameters), the private marker, the
  first intermediate byte, and up to 4 bytes of a pending UTF-8
  sequence.

0.0.3 replaces the single grid with pages of rows for scrollback.

## The font (`include/tam-last/font.h`, `ttf.h`)

- The font file is `mmap`ed read-only; `struct tl_ttf` keeps offsets of
  the tables we read and the chosen cmap subtable.
- `struct tl_outline`: growable arrays of points (x, y in font units,
  on-curve flag) and contour ends, reused for every glyph.
- The glyph cache is an open-addressed hash table keyed by code point and
  width in cells, holding one coverage mask per glyph, exactly cell-sized
  (`cell_w * span` by `cell_h` bytes). It doubles at 70% load up to 16384
  slots, then starts again empty. At 7 x 17 a mask is 119 bytes.

## The keyboard (`include/tam-last/keys.h`)

`struct tl_kbd`: a 128-bit bitmap of the evdev codes held now, and the
Caps Lock and Num Lock states. The layout is a constant table of
`{unshifted, shifted}` characters indexed by evdev code.
