# st

## What it is

suckless's simple terminal, for X11, in C; MIT. About 4,800 lines
(`st.c` for the terminal, `x.c` for X11). Configured by editing
`config.h`. Read from source.

## Ideas worth taking

- **The terminal core is one readable file** — adopt as a goal. `st.c`
  (2,700 lines) holds the parser, screen and tty; the display is
  separate. tam-last keeps the same split: `vt.c` and `term.c` never
  touch Wayland.
- **Clear names for every screen operation** (`tputc`, `tscrollup`,
  `tresize`, `csihandle`) — adopt; ours are `tl_term_print`,
  `scroll_up`, `tl_term_resize`, `tl_term_csi`.

## Things to avoid

- **Configuration by recompiling.** Fred's settings belong in a file.
- **Patches as the way to add features.** st's features live in patch
  sets that conflict with each other; tam-last's live in the tree.
- **The key table in `config.h`** (`static Key key[]`, one row per key
  and mode). Like foot's, a formula does the same.

## Where to look

`st.c`: `tputc`, `csihandle`, `tresize`, `ttywrite`; `config.def.h`.
