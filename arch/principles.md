# Design principles

tam-last is built fresh, from the ground up. We learn from foot, kitty, st,
ghostty and the rest, and take their best code, but we do not inherit
their structure. These rules decide how code is written here.

## Own the path

- Every hop a key press takes inside tam-last is our code: the Wayland
  wire, the keyboard state, the layout and the encoding. Nothing is
  hidden in a library we cannot read in an afternoon.
- The path is documented (`arch/key-path.md`) and will be visible while
  it runs (`tam-last --keys`, P2).
- The bytes a program receives are tested, including the round trip
  through libtam's `tam_key`, the decoder our own programs use.

## Untrusted input stays harmless

Three things reach tam-last from outside, and none is trusted:

- **The compositor's messages.** Every size, string and array is checked
  against the message (`src/wl/conn.c`); a malformed message ends the
  connection with a clear error, never a read past the buffer. File
  descriptors are taken in order and closed when not wanted.
- **Font files.** Every table offset is checked against the file. Outline
  size, composite depth and cache size have limits. The tests feed every
  truncation and thousands of damaged copies of a font.
- **Program output.** Anything a program prints, including `cat` of a
  hostile file, is parsed with capped parameters and lengths. Output can
  never write files, run commands, or reach the clipboard without a
  policy (OSC 52, P3). The parser is fuzzed in the tests.

## Correct before clever

- No undefined behaviour: the tests run under ASan and UBSan
  (`make sanitize`).
- Every system call is checked; `EINTR` is retried and short writes are
  finished.
- Signal handlers only write to a pipe (`src/main.c`).
- tam-last has one thread. Drawing is fast enough on one core; threads
  come only if measurements show they are needed.

## Small modules, explicit state

- One job per file. No source file over about 800 lines.
- State is passed explicitly. The only global is the SIGCHLD pipe.
- Pure logic (wire format, parser and screen, fonts, keys) runs without
  a display and is unit tested.
- Names start with `tl_`. Headers live in `include/tam-last/`.

## Every dependency earns its place

- The goal is the C library and libtam, nothing else, and 0.0.1 meets
  it: `readelf -d build/tam-last` lists only `libc.so.6` and
  `libm.so.6`. libtam is linked statically.
- `arch/dependencies.md` lists each library, why, and its planned exit.
- Plain Make only. No generated code so far; our own protocol scanner
  only when hand-written requests stop being the smaller job.

## Energy is a feature

- An idle terminal sleeps in `poll()` with no timeout. The only timer is
  key repeat, and only while a key is held.
- Drawing happens only when something changed, at most once per frame
  callback. No cursor blinking by default.
- Glyphs are rasterized once and cached; the font file is mapped, not
  copied.

## Helpful errors

- Messages name what failed and why: "cannot connect to
  /run/user/1000/wayland-1: No such file or directory", "font.otf: not a
  TrueType font (CFF outlines are not supported yet)".
