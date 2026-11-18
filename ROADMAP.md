# tam-last roadmap

Each milestone has a version number. A phase (see `plans/`) covers a range
of versions. Versions follow semantic versioning: during 0.x, the minor
number marks a phase's big step and the patch number marks each
milestone inside it.

**Current version:** 0.0.2 (the skeleton with sharp text on scaled
outputs, B1; 2026-11-17). Fred found its text as crisp as foot's, so
FreeType stays out. Development continues on branch `dev`.

## Decided

Fred decided these on 2026-11-17:

- tam-last is our own Wayland terminal, in C, with no dependency other
  than the standard C libraries and libtam. The point is full control over
  how key presses get from evdev to our TUI apps, and the fun of exploring
  that path.
- foot is the primary reference; other terminals are read for ideas.
- Keys are mapped from evdev codes by our own compiled-in keymap. The
  compositor's XKB keymap is not read.
- Text is drawn from TrueType fonts by our own rasterizer from the start.
  FreeType is the fallback: it comes in only if Fred, comparing tam-last
  and foot side by side, finds our text worse. He compared 0.0.2 the same
  day and found it as crisp as foot's.
- The repository copies the structure of tam-pane and tam-clam.

The versions below are a proposal. Fred has not yet set when tam-last
enters a Tamlinux release.

## Milestones

### Phase P1: foundation (0.0.x), `plans/P1-foundation.md`

- **0.0.1** The skeleton: our own Wayland client, TrueType rasterizer,
  US keymap and legacy key encoding; a shell runs on a pty; cursor
  movement, erasing, insert and delete, colors and attributes, wide
  characters.
- **0.0.2** A fix to 0.0.1: sharp text on scaled outputs, through
  fractional-scale-v1 and viewporter (B1).
- **0.0.3** Full-screen programs: the alternate screen, scroll regions,
  origin and insert modes, DEC line-drawing characters, combining
  characters, cursor styles, and damage tracking so only changed cells
  are drawn. Enough for vim, htop, less, yazi and our tam TUIs.
- **0.0.4** Scrollback, the pointer and wheel, selection, the clipboard
  and primary selection, bracketed paste, and mouse reporting.
- **0.0.5** Integer HiDPI for compositors without fractional scaling
  (which 0.0.1 already has), bold
  and italic faces, a fallback font for missing characters, box-drawing
  and block characters drawn by code, and colors and options from a
  configuration file (`tam_ini`), starting from the Tamlinux theme.

### Phase P2: the key path (0.1.x), `plans/P2-key-path.md`

- **0.1.0** The kitty keyboard protocol: progressive enhancement flags,
  CSI u, release and repeat events, alternate keys, the flags stack.
- **0.1.1** `tam-last --keys`: a mode that shows each hop of every key
  press (evdev code, held modifiers, character, bytes written).
- **0.1.2** More layouts, compiled at build time from a compact source
  format, with AltGr, dead keys and compose.
- **0.1.3** Input methods (text-input-v3), for characters no keyboard
  has.

### Phase P3: daily parity with foot (0.2.x), `plans/P3-daily-parity.md`

- **0.2.0** Window titles, OSC 7 (working directory), OSC 8 (links),
  OSC 52 (clipboard, with a policy), color queries and changes.
- **0.2.1** Synchronized output (DEC 2026), device and version queries
  (DA2, XTVERSION), and our own terminfo entry.
- **0.2.2** Search in scrollback, URL mode, notifications.
- **0.2.3** A server mode, so new windows start instantly and share the
  font cache, as `foot --server` does.

### Phase P4: dogfood (0.3.x), `plans/P4-dogfood.md`

- **0.3.0** Packaged in `tamlinux-packages`. Fred uses tam-last in place
  of foot, with foot kept installed as the fallback.
- **0.3.x** Fixes from daily use, recorded in `bugs/`.

### Phase P5: the Tamlinux terminal (1.0.0), `plans/P5-tamlinux-terminal.md`

- **1.0.0** tam-last replaces foot as Tamlinux's terminal. This happens
  only on Fred's Test, Run and Release words.

## How a milestone is done

A milestone is done when its acceptance checks in the phase plan pass and
Fred has tried the build.

Every change to the code bumps `VERSION` in the same commit, so each
build Fred runs says which one it is (`tam-last --version`, checked by
`make test`). A milestone takes its version when it lands; a fix between
milestones takes the next patch number, and the milestones after it
move up one, as 0.0.2 (B1) did.
