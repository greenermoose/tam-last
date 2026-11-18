# The key path

This is the story tam-last exists to tell: how a finger on a key becomes
bytes a program reads, and how the program's answer becomes light on the
screen. Each hop says what happens there, what the data looks like, who
controls it today, and where to watch it.

The example throughout is Ctrl+C, typed into tam-clam or bash running in
tam-last, under tam-pane or Hyprland.

## 1. The keyboard

The keyboard's firmware scans its switch matrix, debounces, and sends a
USB HID report: the modifier byte (Left Ctrl is bit 0) and the usage IDs
of the keys held. C is usage `0x06` on the keyboard page (`0x07`). The
report says which *positions* are down; it knows nothing of letters.

- **Control:** the keyboard's firmware. Not ours, unless the keyboard
  runs QMK or ZMK.
- **Watch:** `usbhid-dump`, or the kernel's `hidraw` device.

## 2. The kernel's input layer

`usbhid` hands the report to `hid-input`, which maps each HID usage to an
evdev key code: usage `0x06` becomes `KEY_C` (46), Left Ctrl becomes
`KEY_LEFTCTRL` (29). The device node `/dev/input/eventN` then yields
`struct input_event` records: type `EV_KEY`, the code, and a value of 1
(press), 0 (release) or 2 (the kernel's autorepeat).

These numbers are a stable kernel ABI. tam-last keeps its own copy of
them (`include/tam-last/evdev.h`) and checks it against the kernel's
header in `tests/test_keys.c`.

On a text VT, the kernel's own keyboard driver
(`drivers/tty/vt/keyboard.c`) turns these codes into characters with the
console keymap (`loadkeys`) and feeds the console's tty. Under a
compositor that path is switched off, and the compositor reads the
device directly.

- **Control:** the kernel. Ours to configure (hwdb remaps), not to write.
- **Watch:** `evtest /dev/input/eventN` or `libinput debug-events` (both
  need read access to the device).

## 3. The compositor

The compositor (Hyprland today, tam-pane tomorrow) reads evdev through
libinput, runs xkbcommon over its keymap, and first checks its own key
bindings. Meta+Return never reaches a client: the compositor eats it.
Ctrl+C is not bound, so it goes to the window with keyboard focus as
Wayland events on `wl_keyboard`:

- `keymap` (once, and when the layout changes): an XKB keymap as text, in
  a file descriptor. **tam-last closes it unread** (`src/window.c`).
- `enter`: focus arrives, with the keys already held.
- `key`: serial, time, the **evdev code unchanged** (29, then 46), and
  pressed or released.
- `modifiers`: the compositor's view of modifiers and locks as XKB
  masks. tam-last uses only the lock bits (Caps Lock is real modifier 1,
  Num Lock is Mod2, bit 4), because locks change while another window
  has the keyboard.
- `repeat_info`: the repeat rate and delay. The compositor does not
  repeat keys for us; the client does.

On the wire each event is a few 32-bit words on the Unix socket: the
object id, the size and opcode, then the arguments.

- **Control:** with tam-pane, ours. With Hyprland, its configuration.
- **Watch:** `WAYLAND_DEBUG=1` works only for programs using libwayland;
  tam-last's own trace arrives with `--keys` (P2).

## 4. tam-last

This is the hop tam-last owns completely.

1. `src/wl/conn.c` reads the socket, checks the message framing, and
   `src/window.c` decodes `wl_keyboard.key`.
2. `src/keys/keymap.c` updates the held-key bitmap. Held modifiers come
   from our own bitmap, not from the compositor's masks: Ctrl is held
   because code 29 is down.
3. `src/keys/encode.c` turns code 46 plus Ctrl into bytes. The US
   layout says code 46 types `c`; Ctrl folds letters into C0 controls,
   so Ctrl+C is the single byte `0x03`. Cursor and function keys become
   CSI or SS3 sequences instead (Up is `ESC [ A`, or `ESC O A` after a
   program sets DECCKM; Ctrl+Up is `ESC [ 1 ; 5 A`).
4. `src/main.c` queues the bytes and writes them to the pty master. If
   the key stays down, `main.c` repeats it on its own timer, using the
   compositor's rate and delay.

The legacy encoding loses information. These presses reach the program
as a different key, and `tests/test_keys.c` lists them:

  Ctrl+H                the same byte as Backspace with Ctrl (0x08)
  Ctrl+I                Tab (0x09)
  Ctrl+M, Ctrl+J        Enter (0x0d, 0x0a)
  Ctrl+[                Escape (0x1b)
  Shift+A               just "A": the Shift is gone
  Ctrl+Shift+A          the same as Ctrl+A
  Meta+anything         the Meta is gone
  any release           not sent at all

The kitty keyboard protocol (P2) lets a program ask for all of these.

- **Control:** ours.
- **Watch:** `tam-last --keys` (P2). Today: `cat -v` or `od -c` in the
  terminal shows the bytes after the tty layer.

## 5. The kernel's tty layer

The pty master is one end of a pseudo-terminal pair; the program holds
the other end (the slave) as its standard input, output and error. In
between sits the line discipline (`n_tty`), and it acts on the bytes
before any program sees them, according to `termios` settings:

- With `ISIG`, `0x03` (the `VINTR` character) is not passed on at all:
  the kernel sends SIGINT to the foreground process group. That is what
  Ctrl+C does. `0x1a` (Ctrl+Z) sends SIGTSTP, `0x1c` (Ctrl+\) SIGQUIT.
- With `ICANON`, the kernel collects a line, handles erase (`VERASE`,
  `0x7f`, which is what our Backspace sends), kill (Ctrl+U) and word
  erase (Ctrl+W), and echoes.
- With `ICRNL`, the `\r` our Enter sends becomes `\n`.
- With `IXON`, Ctrl+S and Ctrl+Q stop and start output.

A line editor (tam-clam's, readline, vim) switches most of this off
(raw mode, libtam's `tam_tty`) and handles the bytes itself; Ctrl+C is
then the byte `0x03` again, if the program leaves `ISIG` off.

When tam-last resizes, `TIOCSWINSZ` on the master makes the kernel send
SIGWINCH to the foreground process group.

- **Control:** the kernel; the program sets the termios flags.
- **Watch:** `stty -a` in the terminal.

## 6. The program

The program `read()`s bytes and decodes them. Our own programs use
libtam's `tam_key`, the exact inverse of hop 4: `tests/test_keys.c`
encodes key presses with tam-last and decodes them with `tam_key`, so a
change on either side that breaks the pair fails the build.

- **Control:** ours for tam-clam and tam; the program's author otherwise.

## The way back: output to light

1. The program writes bytes to the slave. With `OPOST` and `ONLCR`, the
   tty layer turns each `\n` into `\r\n`.
2. tam-last reads the master and feeds `src/vt.c`, a state machine that
   splits the stream into text (UTF-8), controls, and escape sequences.
3. `src/term.c` applies them to the grid of cells: characters with
   colors and attributes, the cursor, scrolling. Queries (cursor
   position, device attributes) are answered by writing back to the pty.
4. When the compositor's last frame callback has come back,
   `src/render.c` draws the grid: each cell's background, then its glyph
   from `src/font/` (rasterized once from the TrueType outline and
   cached), then the cursor.
5. The pixels go into a shared-memory buffer; `wl_surface.attach`,
   `damage_buffer` and `commit` hand it to the compositor, which
   composites it with the other windows and scans the result out to the
   monitor through KMS.

The frame callback is what keeps this cheap: however fast a program
writes, tam-last draws at most once per frame the compositor asks for,
and not at all when nothing changed.
