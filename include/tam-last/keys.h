/* From evdev key codes to the bytes a program reads: our own keyboard
 * state, our own compiled-in layout, and the legacy xterm encoding.
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
	TL_MOD_META = 8
};

/* XKB's fixed real-modifier bits, as wl_keyboard.modifiers reports them:
 * Lock is bit 1 and Mod2 (Num Lock in every standard keymap) is bit 4. */
#define TL_XKB_LOCK (1u << 1)
#define TL_XKB_MOD2 (1u << 4)

#define TL_KEY_SEQ_MAX 64

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

struct tl_kbd {
	uint8_t down[TL_KEY_COUNT / 8];
	bool caps, num;
};

void tl_kbd_init(struct tl_kbd *k);
void tl_kbd_key(struct tl_kbd *k, uint32_t code, bool pressed);
/* Focus arrives with the keys already held. */
void tl_kbd_enter(struct tl_kbd *k, const uint32_t *codes, size_t n);
void tl_kbd_leave(struct tl_kbd *k);
/* Lock states come from the compositor, since they change while another
 * window has the keyboard. */
void tl_kbd_locks(struct tl_kbd *k, uint32_t locked);
/* TL_MOD_* from the keys held now. */
unsigned tl_kbd_mods(const struct tl_kbd *k);
bool tl_key_is_modifier(uint32_t code);

/* The US layout: the character a key types, or 0 for keys that type
 * none. Caps Lock shifts letters only. */
uint32_t tl_keymap_us(uint32_t code, bool shift, bool caps);

struct tl_key_modes {
	bool app_cursor; /* DECCKM */
	bool app_keypad; /* DECKPAM */
	bool num_lock;
	bool caps_lock;
	unsigned kitty_flags; /* TL_KITTY_* bitmask; 0 = legacy xterm */
};

/* The bytes for one key event (press, repeat, or release). Returns their
 * count (0 when the key sends nothing). out has room for TL_KEY_SEQ_MAX. */
size_t tl_key_encode_event(uint32_t code, unsigned mods, enum tl_key_event ev,
	const struct tl_key_modes *m, char out[TL_KEY_SEQ_MAX]);

/* The bytes for one key press. Returns their count (0 when the key sends
 * nothing). out has room for TL_KEY_SEQ_MAX bytes. */
size_t tl_key_encode(uint32_t code, unsigned mods,
	const struct tl_key_modes *m, char out[TL_KEY_SEQ_MAX]);

#endif
