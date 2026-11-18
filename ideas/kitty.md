# kitty

## What it is

A GPU terminal by Kovid Goyal, in C and Python; GPL v3 (compatible with
ours). OpenGL rendering; its own fork of GLFW, including GLFW's Wayland
backend (`glfw/wl_*.c`). The origin of the kitty keyboard protocol, now
also in foot, ghostty, wezterm and alacritty. The protocol was read from
`docs/keyboard-protocol.rst`; the code was only looked through.

## Ideas worth taking

- **The kitty keyboard protocol** — adopt (P2, 0.1.0). Programs ask for
  progressive enhancements with `CSI = flags ; mode u`: disambiguate
  (1), event types with release and repeat (2), alternate keys (4), all
  keys as escape codes (8), associated text (16). A query (`CSI ? u`)
  and a push and pop stack per screen. This is the fix for every loss
  listed in `arch/key-path.md`.
- **Styled and coloured underlines** (`CSI 4:3 m`, `CSI 58`) — consider
  (P3).
- **Bounded stacks against denial of service** — adopt everywhere: the
  protocol's own text asks terminals to cap the flags stack.

## Things to avoid

- **Python in the terminal.** Start-up time and memory.
- **GPU rendering for a terminal.** It keeps the GPU awake; software
  drawing into shm is enough at our sizes, and cheaper when idle.
- **The graphics protocol**, unless Fred wants images in the terminal.

## Where to look

`docs/keyboard-protocol.rst`; `kitty/key_encoding.c` for the encoder.
