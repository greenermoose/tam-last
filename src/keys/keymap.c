/* Keyboard state and the compiled-in US layout.
 *
 * The compositor also sends an XKB keymap, but tam-last does not read it:
 * the evdev code is the ground truth, and the layout is ours. Held
 * modifiers are tracked from our own key events; lock states come from
 * the compositor's modifier event (see keys.h). */
#include "tam-last/keys.h"

#include <string.h>

/* { unshifted, shifted } for each evdev code that types a character. */
static const char us[TL_KEY_COUNT][2] = {
	[TL_KEY_1] = { '1', '!' }, [TL_KEY_2] = { '2', '@' },
	[TL_KEY_3] = { '3', '#' }, [TL_KEY_4] = { '4', '$' },
	[TL_KEY_5] = { '5', '%' }, [TL_KEY_6] = { '6', '^' },
	[TL_KEY_7] = { '7', '&' }, [TL_KEY_8] = { '8', '*' },
	[TL_KEY_9] = { '9', '(' }, [TL_KEY_0] = { '0', ')' },
	[TL_KEY_MINUS] = { '-', '_' }, [TL_KEY_EQUAL] = { '=', '+' },
	[TL_KEY_Q] = { 'q', 'Q' }, [TL_KEY_W] = { 'w', 'W' },
	[TL_KEY_E] = { 'e', 'E' }, [TL_KEY_R] = { 'r', 'R' },
	[TL_KEY_T] = { 't', 'T' }, [TL_KEY_Y] = { 'y', 'Y' },
	[TL_KEY_U] = { 'u', 'U' }, [TL_KEY_I] = { 'i', 'I' },
	[TL_KEY_O] = { 'o', 'O' }, [TL_KEY_P] = { 'p', 'P' },
	[TL_KEY_LEFTBRACE] = { '[', '{' }, [TL_KEY_RIGHTBRACE] = { ']', '}' },
	[TL_KEY_A] = { 'a', 'A' }, [TL_KEY_S] = { 's', 'S' },
	[TL_KEY_D] = { 'd', 'D' }, [TL_KEY_F] = { 'f', 'F' },
	[TL_KEY_G] = { 'g', 'G' }, [TL_KEY_H] = { 'h', 'H' },
	[TL_KEY_J] = { 'j', 'J' }, [TL_KEY_K] = { 'k', 'K' },
	[TL_KEY_L] = { 'l', 'L' },
	[TL_KEY_SEMICOLON] = { ';', ':' }, [TL_KEY_APOSTROPHE] = { '\'', '"' },
	[TL_KEY_GRAVE] = { '`', '~' }, [TL_KEY_BACKSLASH] = { '\\', '|' },
	[TL_KEY_102ND] = { '\\', '|' },
	[TL_KEY_Z] = { 'z', 'Z' }, [TL_KEY_X] = { 'x', 'X' },
	[TL_KEY_C] = { 'c', 'C' }, [TL_KEY_V] = { 'v', 'V' },
	[TL_KEY_B] = { 'b', 'B' }, [TL_KEY_N] = { 'n', 'N' },
	[TL_KEY_M] = { 'm', 'M' },
	[TL_KEY_COMMA] = { ',', '<' }, [TL_KEY_DOT] = { '.', '>' },
	[TL_KEY_SLASH] = { '/', '?' }, [TL_KEY_SPACE] = { ' ', ' ' },
	[TL_KEY_KPASTERISK] = { '*', '*' }, [TL_KEY_KPMINUS] = { '-', '-' },
	[TL_KEY_KPPLUS] = { '+', '+' }, [TL_KEY_KPSLASH] = { '/', '/' },
};

void tl_kbd_init(struct tl_kbd *k)
{
	memset(k, 0, sizeof(*k));
}

void tl_kbd_key(struct tl_kbd *k, uint32_t code, bool pressed)
{
	if (code >= TL_KEY_COUNT)
		return;
	if (pressed)
		k->down[code / 8] |= (uint8_t)(1u << (code % 8));
	else
		k->down[code / 8] &= (uint8_t)~(1u << (code % 8));
}

void tl_kbd_enter(struct tl_kbd *k, const uint32_t *codes, size_t n)
{
	memset(k->down, 0, sizeof(k->down));
	for (size_t i = 0; i < n; i++)
		tl_kbd_key(k, codes[i], true);
}

void tl_kbd_leave(struct tl_kbd *k)
{
	memset(k->down, 0, sizeof(k->down));
}

void tl_kbd_locks(struct tl_kbd *k, uint32_t locked)
{
	k->caps = (locked & TL_XKB_LOCK) != 0;
	k->num = (locked & TL_XKB_MOD2) != 0;
}

static bool held(const struct tl_kbd *k, uint32_t code)
{
	return (k->down[code / 8] >> (code % 8)) & 1u;
}

unsigned tl_kbd_mods(const struct tl_kbd *k)
{
	unsigned m = 0;
	if (held(k, TL_KEY_LEFTSHIFT) || held(k, TL_KEY_RIGHTSHIFT))
		m |= TL_MOD_SHIFT;
	/* On a US keyboard both Alt keys are Alt; AltGr comes with other
	 * layouts (P2). */
	if (held(k, TL_KEY_LEFTALT) || held(k, TL_KEY_RIGHTALT))
		m |= TL_MOD_ALT;
	if (held(k, TL_KEY_LEFTCTRL) || held(k, TL_KEY_RIGHTCTRL))
		m |= TL_MOD_CTRL;
	if (held(k, TL_KEY_LEFTMETA) || held(k, TL_KEY_RIGHTMETA))
		m |= TL_MOD_META;
	return m;
}

bool tl_key_is_modifier(uint32_t code)
{
	switch (code) {
	case TL_KEY_LEFTSHIFT: case TL_KEY_RIGHTSHIFT:
	case TL_KEY_LEFTCTRL: case TL_KEY_RIGHTCTRL:
	case TL_KEY_LEFTALT: case TL_KEY_RIGHTALT:
	case TL_KEY_LEFTMETA: case TL_KEY_RIGHTMETA:
	case TL_KEY_CAPSLOCK: case TL_KEY_NUMLOCK: case TL_KEY_SCROLLLOCK:
		return true;
	default:
		return false;
	}
}

uint32_t tl_keymap_us(uint32_t code, bool shift, bool caps)
{
	if (code >= TL_KEY_COUNT || us[code][0] == 0)
		return 0;
	char base = us[code][0];
	if (caps && base >= 'a' && base <= 'z')
		shift = !shift;
	return (unsigned char)us[code][shift ? 1 : 0];
}
