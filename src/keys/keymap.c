/* Keyboard state and compiled-in layout lookup.
 *
 * The compositor also sends an XKB keymap, but tam-last does not read it:
 * the evdev code is the ground truth, and the layout is ours. Held
 * modifiers are tracked from our own key events; lock states come from
 * the compositor's modifier event (see keys.h). */
#include "tam-last/keys.h"

#include <string.h>

static bool is_caps_pair(uint32_t lo, uint32_t hi)
{
	if (lo >= 'a' && lo <= 'z' && hi == lo - 32u)
		return true;
	if (((lo >= 0x00e0u && lo <= 0x00f6u) ||
	     (lo >= 0x00f8u && lo <= 0x00feu)) &&
	    hi == lo - 0x20u)
		return true;
	if (lo == 0x0153u && hi == 0x0152u) /* œ / Œ */
		return true;
	return false;
}

const struct tl_layout *tl_layout_default(void)
{
	return &tl_layouts[0];
}

const struct tl_layout *tl_layout_find(const char *name)
{
	if (name == NULL || name[0] == '\0')
		return NULL;
	for (size_t i = 0; i < tl_layout_count_val; i++) {
		if (strcmp(tl_layouts[i].name, name) == 0)
			return &tl_layouts[i];
	}
	return NULL;
}

size_t tl_layout_count(void)
{
	return tl_layout_count_val;
}

const struct tl_layout *tl_layout_at(size_t index)
{
	if (index >= tl_layout_count_val)
		return NULL;
	return &tl_layouts[index];
}

const char *tl_dead_key_name(uint32_t sym)
{
	if ((sym & TL_KEY_DEAD_BIT) == 0)
		return NULL;
	switch (sym & ~TL_KEY_DEAD_BIT) {
	case TL_DEAD_ACUTE:      return "dead_acute";
	case TL_DEAD_GRAVE:      return "dead_grave";
	case TL_DEAD_CIRCUMFLEX: return "dead_circumflex";
	case TL_DEAD_TILDE:      return "dead_tilde";
	case TL_DEAD_DIAERESIS:  return "dead_diaeresis";
	case TL_DEAD_CEDILLA:    return "dead_cedilla";
	case TL_DEAD_RING:       return "dead_ring";
	default:                 return NULL;
	}
}

void tl_kbd_init(struct tl_kbd *k)
{
	memset(k, 0, sizeof(*k));
	k->layout = tl_layout_default();
}

void tl_kbd_set_layout(struct tl_kbd *k, const struct tl_layout *layout)
{
	k->layout = layout != NULL ? layout : tl_layout_default();
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
	k->depressed = 0;
	k->have_mods = false;
	for (size_t i = 0; i < n; i++)
		tl_kbd_key(k, codes[i], true);
}

void tl_kbd_leave(struct tl_kbd *k)
{
	memset(k->down, 0, sizeof(k->down));
	k->depressed = 0;
	k->have_mods = false;
}

void tl_kbd_modifiers(struct tl_kbd *k, uint32_t depressed, uint32_t locked)
{
	k->depressed = depressed;
	k->have_mods = true;
	tl_kbd_locks(k, locked);
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
	bool has_altgr = k->layout != NULL && k->layout->has_altgr;
	if (k->have_mods) {
		if (k->depressed & TL_XKB_SHIFT)
			m |= TL_MOD_SHIFT;
		if (k->depressed & TL_XKB_CTRL)
			m |= TL_MOD_CTRL;
		if (k->depressed & TL_XKB_MOD4)
			m |= TL_MOD_META;
		if (has_altgr &&
		    ((k->depressed & TL_XKB_MOD5) || held(k, TL_KEY_RIGHTALT))) {
			m |= TL_MOD_ALTGR;
			if ((k->depressed & TL_XKB_MOD1) &&
			    (!held(k, TL_KEY_RIGHTALT) || held(k, TL_KEY_LEFTALT)))
				m |= TL_MOD_ALT;
		} else if (k->depressed & TL_XKB_MOD1) {
			m |= TL_MOD_ALT;
		}
		return m;
	}
	if (held(k, TL_KEY_LEFTSHIFT) || held(k, TL_KEY_RIGHTSHIFT))
		m |= TL_MOD_SHIFT;
	if (held(k, TL_KEY_LEFTALT))
		m |= TL_MOD_ALT;
	if (held(k, TL_KEY_RIGHTALT))
		m |= has_altgr ? TL_MOD_ALTGR : TL_MOD_ALT;
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

uint32_t tl_layout_lookup(const struct tl_layout *layout, uint32_t code,
	unsigned mods, bool caps)
{
	if (code >= TL_KEY_COUNT)
		return 0;
	const struct tl_layout *l = layout != NULL ? layout : tl_layout_default();
	unsigned base = (mods & TL_MOD_ALTGR) ? 2u : 0u;
	bool shift = (mods & TL_MOD_SHIFT) != 0;
	uint32_t lo = l->map[code][base];
	uint32_t hi = l->map[code][base + 1u];
	if (caps && is_caps_pair(lo, hi))
		shift = !shift;
	return shift ? hi : lo;
}

uint32_t tl_keymap_us(uint32_t code, bool shift, bool caps)
{
	return tl_layout_lookup(tl_layout_default(), code,
		shift ? TL_MOD_SHIFT : 0u, caps);
}
