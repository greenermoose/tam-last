# tam-last roadmap

`tam-last` is Tamlinux's Wayland terminal emulator, written in C17 with
only libc and `libtam`.

- **Current version:** `0.2.5` (branch `dev`)
- **Progress:** 3 of 5 phases done (P1–P3 done; P4 next)
- **Versioning:** Semantic versioning; during 0.x, minor marks a phase and
  patch marks milestones or fixes within it.

## Phase summary

| Phase | Versions | Goal | Plan | Status |
| --- | --- | --- | --- | --- |
| **P1** | 0.0.x | Foundation: Wayland client, TrueType rasterizer, VT emulation, scrollback, looks | [P1-foundation.md](plans/P1-foundation.md) | Done (2026-11-22) |
| **P2** | 0.1.x | Key path: kitty keyboard protocol, `--keys` trace, compiled layouts, IME | [P2-key-path.md](plans/P2-key-path.md) | Done |
| **P3** | 0.2.x | Daily parity with foot: OSCs, sync output, search, server mode, `libtam` adoption | [P3-daily-parity.md](plans/P3-daily-parity.md) | Done (2026-11-26) |
| **P4** | 0.3.x | Dogfooding in place of foot with daily-use fixes | [P4-dogfood.md](plans/P4-dogfood.md) | Planned |
| **P5** | 1.0.0 | Replace foot as Tamlinux's terminal (only on Fred's word) | [P5-tamlinux-terminal.md](plans/P5-tamlinux-terminal.md) | Planned |

## Decided

Fred decided these on 2026-11-17:

- `tam-last` is our own Wayland terminal in C, depending only on libc and
  `libtam`, giving full control over the key path from evdev to TUI apps.
- `foot` is the primary reference; other terminals are read for ideas.
- Keys are mapped from evdev codes by our own compiled-in keymap rather
  than reading the compositor's XKB keymap.
- Text is drawn from TrueType fonts by our own rasterizer; Fred compared
  0.0.2 side by side with `foot` and found it as crisp, so FreeType stays
  out.

## Phases and milestones

### Phase P1: Foundation (0.0.x) — [P1-foundation.md](plans/P1-foundation.md)

| Version | Milestone | Status |
| --- | --- | --- |
| **0.0.1** | Skeleton: Wayland client, TrueType rasterizer, US keymap, pty shell, cursor/color/wide-char emulation | Done (2026-11-17) |
| **0.0.2** | Sharp text on scaled outputs via fractional-scale-v1 and viewporter (B1) | Done (2026-11-17) |
| **0.0.3** | Full-screen programs: alt screen, scroll regions, DEC line drawing, combining chars, cursor styles, damage tracking | Done (2026-11-21) |
| **0.0.4** | Scrollback, pointer and wheel, selection, clipboard/primary selection, bracketed paste, mouse reporting | Done (2026-11-21) |
| **0.0.5** | HiDPI, bold/italic faces, fallback font, drawn box/block chars, and `tam_ini` configuration | Done (2026-11-22) |

### Phase P2: The key path (0.1.x) — [P2-key-path.md](plans/P2-key-path.md)

| Version | Milestone | Status |
| --- | --- | --- |
| **0.1.0** | Kitty keyboard protocol: progressive flags, CSI u, release/repeat events, alternate keys, flags stack | Done |
| **0.1.1** | `tam-last --keys` trace mode showing every hop of a key press | Done |
| **0.1.2** | Compiled keyboard layouts with AltGr, dead keys, and compose | Done |
| **0.1.3** | Input methods via `text-input-v3` | Done |

### Phase P3: Daily parity with foot (0.2.x) — [P3-daily-parity.md](plans/P3-daily-parity.md)

| Version | Milestone | Status |
| --- | --- | --- |
| **0.2.0** | Window titles, OSC 7 (cwd), OSC 8 (links), OSC 52 (clipboard policy), color queries/changes | Done |
| **0.2.1** | Synchronized output (DEC 2026), DA2, XTVERSION, DECRQM, and terminfo entry | Done |
| **0.2.2** | Scrollback search, URL mode, and OSC 777/99 notifications | Done |
| **0.2.3** | Server mode with shared font cache and client IPC | Done |
| **0.2.4** | Honor depressed modifiers and align clipboard shortcuts | Done |
| **0.2.5** | Non-blocking selection reader, `libtam` shared-code adoption, and synthetic AltGr modifier masks | Done (2026-11-26) |

### Phase P4: Dogfood (0.3.x) — [P4-dogfood.md](plans/P4-dogfood.md)

| Version | Milestone | Status |
| --- | --- | --- |
| **0.3.0** | Packaged for Tamlinux; Fred uses `tam-last` in place of `foot` (with `foot` kept as fallback) | Planned |
| **0.3.x** | Fixes from daily use, recorded in `bugs/` | Planned |

### Phase P5: The Tamlinux terminal (1.0.0) — [P5-tamlinux-terminal.md](plans/P5-tamlinux-terminal.md)

| Version | Milestone | Status |
| --- | --- | --- |
| **1.0.0** | `tam-last` replaces `foot` as Tamlinux's terminal (only on Fred's word) | Planned |

## How a milestone is done

A milestone is done when its acceptance checks in the phase plan pass and
Fred has tried the build. When a milestone lands, bump `VERSION`, update
the phase plan's status, and update this roadmap in the same change.
