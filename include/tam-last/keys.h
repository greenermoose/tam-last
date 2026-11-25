/* From evdev key codes to the bytes a program reads: our own keyboard
 * state, compiled-in layouts (with AltGr, dead keys, and compose), and
 * legacy xterm + kitty keyboard protocol encoding.
 * arch/key-path.md describes the whole path. */
#ifndef TL_KEYS_H
#define TL_KEYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tam-last/evdev.h"

enum {
	TL_MOD_SHIFT = 1,
	TL_MOD_ALT = 2,
	TL_MOD_CTRL = 4,
	TL_MOD_META = 8,
	TL_MOD_ALTGR = 16
};

/* XKB's fixed real-modifier bits, as wl_keyboard.modifiers reports them. */
#define TL_XKB_SHIFT (1u << 0)
#define TL_XKB_LOCK  (1u << 1)
#define TL_XKB_CTRL  (1u << 2)
#define TL_XKB_MOD1  (1u << 3)
#define TL_XKB_MOD2  (1u << 4)
#define TL_XKB_MOD4  (1u << 6)
#define TL_XKB_MOD5  (1u << 7)

#define TL_KEY_SEQ_MAX 64

/* High bit marks a non-character symbol (dead key or Compose trigger). */
#define TL_KEY_DEAD_BIT  0x80000000u
#define TL_COMPOSE_START (TL_KEY_DEAD_BIT | 0u)

enum tl_dead_key {
	TL_DEAD_ACUTE = 1,
	TL_DEAD_GRAVE,
	TL_DEAD_CIRCUMFLEX,
	TL_DEAD_TILDE,
	TL_DEAD_DIAERESIS,
	TL_DEAD_CEDILLA,
	TL_DEAD_RING,
	TL_DEAD_COUNT
};

enum {
	TL_KITTY_DISAMBIGUATE = 1u << 0,
	TL_KITTY_EVENTS       = 1u << 1,
	TL_KITTY_ALTERNATE    = 1u << 2,
	TL_KITTY_ALL_KEYS     = 1u << 3,
	TL_KITTY_TEXT         = 1u << 4,
	TL_KITTY_MASK         = 0x1fu
};

enum tl_key_event {
	TL_KEY_PRESS   = 1,
	TL_KEY_REPEAT  = 2,
	TL_KEY_RELEASE = 3
};

struct tl_layout {
	const char *name;
	bool has_altgr;
	/* Levels: [0]=base, [1]=Shift, [2]=AltGr, [3]=AltGr+Shift. */
	uint32_t map[TL_KEY_COUNT][4];
};

#define TL_COMPOSE_MAX 4

struct tl_compose_rule {
	uint8_t len;
	uint32_t seq[TL_COMPOSE_MAX];
	uint32_t result;
};

enum tl_compose_status {
	TL_COMPOSE_IDLE = 0,
	TL_COMPOSE_PENDING,
	TL_COMPOSE_DONE,
	TL_COMPOSE_CANCELLED
};

struct tl_compose {
	uint32_t seq[TL_COMPOSE_MAX];
	uint8_t len;
	bool active;
};

/* Generated layout and compose tables (build/layouts_gen.c). */
extern const struct tl_layout tl_layouts[];
extern const size_t tl_layout_count_val;
extern const struct tl_compose_rule tl_compose_rules[];
extern const size_t tl_compose_rule_count_val;

struct tl_kbd {
	uint8_t down[TL_KEY_COUNT / 8];
	uint32_t depressed;
	bool have_mods;
	bool caps, num;
	const struct tl_layout *layout;
};

void tl_kbd_init(struct tl_kbd *k);
void tl_kbd_set_layout(struct tl_kbd *k, const struct tl_layout *layout);
void tl_kbd_key(struct tl_kbd *k, uint32_t code, bool pressed);
/* Focus arrives with the keys already held. */
void tl_kbd_enter(struct tl_kbd *k, const uint32_t *codes, size_t n);
void tl_kbd_leave(struct tl_kbd *k);
/* Modifier and lock states from wl_keyboard.modifiers. */
void tl_kbd_modifiers(struct tl_kbd *k, uint32_t depressed, uint32_t locked);
/* Lock states come from the compositor, since they change while another
 * window has the keyboard. */
void tl_kbd_locks(struct tl_kbd *k, uint32_t locked);
/* TL_MOD_* from the keys held now. */
unsigned tl_kbd_mods(const struct tl_kbd *k);
bool tl_key_is_modifier(uint32_t code);

/* Compiled-in layout lookup. */
const struct tl_layout *tl_layout_default(void);
const struct tl_layout *tl_layout_find(const char *name);
size_t tl_layout_count(void);
const struct tl_layout *tl_layout_at(size_t index);

/* Look up the symbol (Unicode codepoint or TL_KEY_DEAD_BIT | dead_id, or 0)
 * for an evdev key under mods (Shift, AltGr) and Caps Lock.
 * NULL layout uses the default US layout. */
uint32_t tl_layout_lookup(const struct tl_layout *layout, uint32_t code,
	unsigned mods, bool caps);

/* Name of a dead-key symbol ("dead_acute", ...), or NULL if not a dead key. */
const char *tl_dead_key_name(uint32_t sym);

/* The US layout: the character a key types, or 0 for keys that type
 * none. Caps Lock shifts letters only. */
uint32_t tl_keymap_us(uint32_t code, bool shift, bool caps);

/* Dead-key and Compose sequence state machine. */
void tl_compose_init(struct tl_compose *c);
void tl_compose_reset(struct tl_compose *c);
bool tl_compose_active(const struct tl_compose *c);
enum tl_compose_status tl_compose_feed(struct tl_compose *c, uint32_t sym,
	uint32_t *out_cp);

struct tl_key_modes {
	bool app_cursor; /* DECCKM */
	bool app_keypad; /* DECKPAM */
	bool num_lock;
	bool caps_lock;
	unsigned kitty_flags; /* TL_KITTY_* bitmask; 0 = legacy xterm */
	const struct tl_layout *layout; /* NULL = default US layout */
};

/* Encode one Unicode scalar value as UTF-8 (1..4 bytes, 0 if invalid). */
size_t tl_utf8_encode(uint32_t cp, char out[4]);

/* Encode a composed/dead-key Unicode character as PTY bytes. */
size_t tl_key_encode_text(uint32_t cp, char out[TL_KEY_SEQ_MAX]);

/* The bytes for one key event (press, repeat, or release). Returns their
 * count (0 when the key sends nothing). out has room for TL_KEY_SEQ_MAX. */
size_t tl_key_encode_event(uint32_t code, unsigned mods, enum tl_key_event ev,
	const struct tl_key_modes *m, char out[TL_KEY_SEQ_MAX]);

/* The bytes for one key press. Returns their count (0 when the key sends
 * nothing). out has room for TL_KEY_SEQ_MAX bytes. */
size_t tl_key_encode(uint32_t code, unsigned mods,
	const struct tl_key_modes *m, char out[TL_KEY_SEQ_MAX]);

/* Linux evdev name ("KEY_A", "KEY_LEFTCTRL", ...) or NULL if unknown. */
const char *tl_evdev_name(uint32_t code);

/* The character or dead-key symbol a key produces under the current layout,
 * modifiers, and locks (including keypad digits when Num Lock is on), or 0. */
uint32_t tl_key_char(uint32_t code, unsigned mods,
	const struct tl_key_modes *m);

/* Format one key-event trace line for --keys into buf (always NUL-terminated
 * when cap > 0). Returns the number of characters written (excluding NUL). */
size_t tl_key_trace_format(char *buf, size_t cap, uint32_t code,
	unsigned mods, enum tl_key_event ev, const struct tl_key_modes *m,
	const char *pty_bytes, size_t pty_len, const char *note);

/* Format one input-method (text-input-v3) trace line for --keys. */
size_t tl_key_trace_ime(char *buf, size_t cap, const char *kind,
	const char *text, size_t len, int32_t cursor_begin, int32_t cursor_end);

#endif
