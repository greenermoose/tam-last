# ghostty

## What it is

A terminal by Mitchell Hashimoto, in Zig; MIT. GPU rendering, native
toolkits (GTK on Linux), and libghostty-vt, its terminal core as a
library. Looked through for structure only (starter note; read the
source before acting).

## Ideas worth taking

- **Scrollback in fixed-size memory pages** (`src/terminal/PageList.zig`,
  `page.zig`) — consider (P1, 0.0.4). Pages are allocated and freed as a
  unit, which keeps memory bounded and cheap to trim.
- **The terminal core as a separate library** — consider. tam-last's
  `vt.c` and `term.c` could become a libtam module that tam-pane or tam
  could use (for a built-in terminal or a screen recorder).
- **A generated parse table** (`parse_table.zig`) — reject for now. A
  `switch` per state is easier to read and fast enough.
- **One file per concern for keys** (`src/input/key_encode.zig`,
  `function_keys.zig`, `kitty.zig`) — adopt; ours are `keys/keymap.c`,
  `keys/encode.c`, and `keys/kitty.c` in P2.

## Things to avoid

- GPU rendering and a large toolkit, for the same reasons as kitty.

## Where to look

`src/terminal/Parser.zig`, `PageList.zig`, `src/input/key_encode.zig`.
