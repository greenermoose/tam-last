# P3: Daily parity with foot (0.2.x)

**Status:** not started.

## Goal

Everything Fred uses foot for each day works in tam-last.

## Versions

- **0.2.0** OSC: window title (0, 2), working directory (7), hyperlinks
  (8), clipboard (52, write-only by default, reading only when allowed),
  color queries and changes (4, 10, 11, 12, 104).
- **0.2.1** Synchronized output (DEC 2026), DA2 and XTVERSION, DECRQM;
  our own terminfo entry `tam-last`, shipped with the package.
- **0.2.2** Search in scrollback, URL mode (open links by keyboard),
  desktop notifications (OSC 777 or 99) through `notify-send` with a
  deadline.
- **0.2.3** Server mode: one process, many windows, shared font cache,
  instant start (`tam-last --server` and a client).

## Acceptance

- Fred's daily programs (tam-clam or bash, vim, yazi, htop, git, tam,
  ssh) run without visible differences from foot.

## Dependencies

- None added.

## Open points

- Which notification path, given Tamlinux's Quickshell shell.
- Whether Fred wants Sixel or kitty graphics at all (foot has Sixel).
