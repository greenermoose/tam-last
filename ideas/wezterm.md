# wezterm

## What it is

A GPU terminal and multiplexer in Rust; MIT. Lua configuration, SSH and
serial domains, tabs and panes. Its terminal library is `termwiz`.
Looked through for structure only (starter note).

## Ideas worth taking

- **A terminal library with its own line editor and keymap**
  (`termwiz/src/lineedit`, `keymap.rs`) — consider. The same split we
  have between libtam (`tam_key`, `tam_tty`) and our programs.

## Things to avoid

- **Doing everything.** Multiplexing belongs to the compositor (tam-pane)
  and to tmux if wanted; a terminal should stay small.

## Where to look

`termwiz/src/input.rs` (key decoding on the program's side),
`termwiz/src/escape/`.
