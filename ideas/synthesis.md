# Synthesis: the best ideas, by topic

Start here. Each line points to the file with the details.

## The key path

- Map evdev codes with our own tables, as the Linux console has always
  done (`linux-console.md`); the compositor's XKB keymap is not read.
  Decided by Fred on 2026-11-17. (P1, 0.0.1)
- Legacy encoding by formula, not by table (`foot.md`, `st.md`,
  `xterm.md`). (P1, 0.0.1)
- The kitty keyboard protocol for programs that ask (`kitty.md`), with a
  bounded flags stack. (P2, 0.1.0)
- Show every hop while it happens: `tam-last --keys`. Ours; no other
  terminal does this. (P2, 0.1.1)
- Keep the encoder and libtam's decoder tested as a pair. Ours.

## Text

- No FreeType: a glyf rasterizer of our own (havoc proves it works with
  stb_truetype, `havoc.md`), with vertical grid fitting. (P1, 0.0.1)
- A fallback TrueType font compiled in, so tam-last always starts
  (`havoc.md`). (consider, P1 0.0.4)
- Box-drawing characters drawn by code (`foot.md`). (P1, 0.0.4)

## The screen and memory

- Draw only on the frame callback, only when something changed
  (`foot.md`). (P1, 0.0.1)
- Scrollback as a ring of rows (`foot.md`, `alacritty.md`) or as pages
  (`ghostty.md`). (P1, 0.0.3)
- Scroll damage as pixel moves; possibly foot's zero-copy buffer
  scrolling (`foot.md`). (P1, 0.0.2)

## Structure

- The parser and screen never touch the display (`st.md`,
  `alacritty.md`), so they could one day be a libtam module
  (`ghostty.md`).
- One small configuration file (`havoc.md`), read with `tam_ini`.
- Server mode for instant windows (`foot.md`). (P3, 0.2.3)

## What we leave out

- GPU rendering (`kitty.md`, `ghostty.md`, `alacritty.md`): it keeps the
  GPU awake for little gain at terminal sizes.
- Configuration by recompiling (`st.md`), embedded scripting languages
  (`kitty.md`, `wezterm.md`), multiplexing (`wezterm.md`).
