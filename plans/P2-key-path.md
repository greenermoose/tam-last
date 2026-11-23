# P2: The key path (0.1.x)

**Status:** implemented (0.1.0, 0.1.1, 0.1.2, 0.1.3).

## Goal

Own and show every hop from evdev to our programs. Programs that ask can
tell every key and modifier apart; Fred can watch a key press travel
through tam-last.

## Versions

- **0.1.0 The kitty keyboard protocol.** `CSI = flags ; mode u`, the
  query `CSI ? u`, push and pop with a bounded stack per screen. The five
  enhancements: disambiguate, event types (press, repeat, release),
  alternate keys, all keys as escape codes, associated text. Encoding
  follows kitty's `docs/keyboard-protocol.rst`; foot's
  `kitty_kbd_protocol` is the C reference.
- **0.1.1 `tam-last --keys`.** A mode (or an overlay) that prints, for
  every key event: the evdev code and its name, press or release, held
  modifiers and locks, the layout's character, and the exact bytes
  written to the pty. The same data as a debug log line.
- **0.1.2 Layouts.** A compact layout source (one line per key: evdev
  name, levels), compiled at build time into tables; US, US
  international, and the layouts Fred names. AltGr, dead keys and a
  compose table. Shared with tam-pane's P6 keymaps (open point).
- **0.1.3 Input methods.** text-input-v3, so an input method can send
  text the keyboard cannot type.

## Acceptance

- Every legacy loss listed in `tests/test_keys.c` (Ctrl+H, Ctrl+I,
  Ctrl+M, Ctrl+Shift+letter, Ctrl+Backspace) becomes distinct under the
  disambiguate flag, checked by a round trip through a kitty-aware
  decoder.
- `--keys` output for a key press matches what the program receives.

## Dependencies

- None added.

## Open points

- Whether libtam's `tam_key` learns to decode the kitty protocol, so our
  own programs (tam-clam, tam) can ask for it. That change belongs to
  libtam.
- Where layouts live: tam-last, libtam, or a data package shared with
  tam-pane.
