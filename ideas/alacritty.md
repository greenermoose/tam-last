# alacritty

## What it is

A GPU terminal in Rust; Apache 2.0 or MIT. The parser is the separate
`vte` crate; the terminal core is the `alacritty_terminal` crate. Looked
through for structure only (starter note).

## Ideas worth taking

- **The grid as a ring buffer of rows with a zero offset**
  (`alacritty_terminal/src/grid/storage.rs`) — adopt (P1, 0.0.4), the
  same idea as foot's.
- **The parser as a separate, reusable unit** — adopt; `vt.c` already
  knows nothing about the screen.
- **Vi mode for moving through scrollback** — consider (P3).

## Things to avoid

- GPU rendering; a configuration language with live reload of
  everything.

## Where to look

`alacritty_terminal/src/grid/`, `alacritty/src/input/keyboard.rs`.
