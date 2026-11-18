# xterm

## What it is

The reference terminal for X11, maintained by Thomas Dickey; MIT-style.
Starter note from its documentation, not its source: "XTerm Control
Sequences" (ctlseqs) is the de facto standard every terminal follows.

## Ideas worth taking

- **ctlseqs as the specification** — adopt (P1). Where DEC manuals and
  xterm differ, tam-last follows xterm, because `TERM` says
  `xterm-256color`.
- **The modifier parameter `1 + Shift + 2 Alt + 4 Ctrl + 8 Meta`** —
  adopt (P1, 0.0.1).
- **modifyOtherKeys** — reject. The kitty protocol does the same job
  better, and foot and others now prefer it.

## Where to look

https://invisible-island.net/xterm/ctlseqs/ctlseqs.html; the xterm FAQ
on terminfo and `TERM`.
