/* Key-event tracing for tam-last --keys: evdev code names, layout character
 * lookup, and deterministic one-line formatting shared by stderr logs and the
 * bottom overlay bar. */
#include "tam-last/keys.h"

#include <stdio.h>

static const char *const evdev_names[TL_KEY_COUNT] = {
	[TL_KEY_ESC] = "KEY_ESC",
	[TL_KEY_1] = "KEY_1", [TL_KEY_2] = "KEY_2", [TL_KEY_3] = "KEY_3",
	[TL_KEY_4] = "KEY_4", [TL_KEY_5] = "KEY_5", [TL_KEY_6] = "KEY_6",
	[TL_KEY_7] = "KEY_7", [TL_KEY_8] = "KEY_8", [TL_KEY_9] = "KEY_9",
	[TL_KEY_0] = "KEY_0",
	[TL_KEY_MINUS] = "KEY_MINUS", [TL_KEY_EQUAL] = "KEY_EQUAL",
	[TL_KEY_BACKSPACE] = "KEY_BACKSPACE", [TL_KEY_TAB] = "KEY_TAB",
	[TL_KEY_Q] = "KEY_Q", [TL_KEY_W] = "KEY_W", [TL_KEY_E] = "KEY_E",
	[TL_KEY_R] = "KEY_R", [TL_KEY_T] = "KEY_T", [TL_KEY_Y] = "KEY_Y",
	[TL_KEY_U] = "KEY_U", [TL_KEY_I] = "KEY_I", [TL_KEY_O] = "KEY_O",
	[TL_KEY_P] = "KEY_P",
	[TL_KEY_LEFTBRACE] = "KEY_LEFTBRACE",
	[TL_KEY_RIGHTBRACE] = "KEY_RIGHTBRACE",
	[TL_KEY_ENTER] = "KEY_ENTER", [TL_KEY_LEFTCTRL] = "KEY_LEFTCTRL",
	[TL_KEY_A] = "KEY_A", [TL_KEY_S] = "KEY_S", [TL_KEY_D] = "KEY_D",
	[TL_KEY_F] = "KEY_F", [TL_KEY_G] = "KEY_G", [TL_KEY_H] = "KEY_H",
	[TL_KEY_J] = "KEY_J", [TL_KEY_K] = "KEY_K", [TL_KEY_L] = "KEY_L",
	[TL_KEY_SEMICOLON] = "KEY_SEMICOLON",
	[TL_KEY_APOSTROPHE] = "KEY_APOSTROPHE",
	[TL_KEY_GRAVE] = "KEY_GRAVE",
	[TL_KEY_LEFTSHIFT] = "KEY_LEFTSHIFT",
	[TL_KEY_BACKSLASH] = "KEY_BACKSLASH",
	[TL_KEY_Z] = "KEY_Z", [TL_KEY_X] = "KEY_X", [TL_KEY_C] = "KEY_C",
	[TL_KEY_V] = "KEY_V", [TL_KEY_B] = "KEY_B", [TL_KEY_N] = "KEY_N",
	[TL_KEY_M] = "KEY_M",
	[TL_KEY_COMMA] = "KEY_COMMA", [TL_KEY_DOT] = "KEY_DOT",
	[TL_KEY_SLASH] = "KEY_SLASH",
	[TL_KEY_RIGHTSHIFT] = "KEY_RIGHTSHIFT",
	[TL_KEY_KPASTERISK] = "KEY_KPASTERISK",
	[TL_KEY_LEFTALT] = "KEY_LEFTALT", [TL_KEY_SPACE] = "KEY_SPACE",
	[TL_KEY_CAPSLOCK] = "KEY_CAPSLOCK",
	[TL_KEY_F1] = "KEY_F1", [TL_KEY_F2] = "KEY_F2",
	[TL_KEY_F3] = "KEY_F3", [TL_KEY_F4] = "KEY_F4",
	[TL_KEY_F5] = "KEY_F5", [TL_KEY_F6] = "KEY_F6",
	[TL_KEY_F7] = "KEY_F7", [TL_KEY_F8] = "KEY_F8",
	[TL_KEY_F9] = "KEY_F9", [TL_KEY_F10] = "KEY_F10",
	[TL_KEY_NUMLOCK] = "KEY_NUMLOCK",
	[TL_KEY_SCROLLLOCK] = "KEY_SCROLLLOCK",
	[TL_KEY_KP7] = "KEY_KP7", [TL_KEY_KP8] = "KEY_KP8",
	[TL_KEY_KP9] = "KEY_KP9", [TL_KEY_KPMINUS] = "KEY_KPMINUS",
	[TL_KEY_KP4] = "KEY_KP4", [TL_KEY_KP5] = "KEY_KP5",
	[TL_KEY_KP6] = "KEY_KP6", [TL_KEY_KPPLUS] = "KEY_KPPLUS",
	[TL_KEY_KP1] = "KEY_KP1", [TL_KEY_KP2] = "KEY_KP2",
	[TL_KEY_KP3] = "KEY_KP3", [TL_KEY_KP0] = "KEY_KP0",
	[TL_KEY_KPDOT] = "KEY_KPDOT",
	[TL_KEY_102ND] = "KEY_102ND",
	[TL_KEY_F11] = "KEY_F11", [TL_KEY_F12] = "KEY_F12",
	[TL_KEY_KPENTER] = "KEY_KPENTER",
	[TL_KEY_RIGHTCTRL] = "KEY_RIGHTCTRL",
	[TL_KEY_KPSLASH] = "KEY_KPSLASH",
	[TL_KEY_SYSRQ] = "KEY_SYSRQ", [TL_KEY_RIGHTALT] = "KEY_RIGHTALT",
	[TL_KEY_HOME] = "KEY_HOME", [TL_KEY_UP] = "KEY_UP",
	[TL_KEY_PAGEUP] = "KEY_PAGEUP", [TL_KEY_LEFT] = "KEY_LEFT",
	[TL_KEY_RIGHT] = "KEY_RIGHT", [TL_KEY_END] = "KEY_END",
	[TL_KEY_DOWN] = "KEY_DOWN", [TL_KEY_PAGEDOWN] = "KEY_PAGEDOWN",
	[TL_KEY_INSERT] = "KEY_INSERT", [TL_KEY_DELETE] = "KEY_DELETE",
	[TL_KEY_LEFTMETA] = "KEY_LEFTMETA",
	[TL_KEY_RIGHTMETA] = "KEY_RIGHTMETA",
	[TL_KEY_COMPOSE] = "KEY_COMPOSE",
};

const char *tl_evdev_name(uint32_t code)
{
	if (code >= TL_KEY_COUNT)
		return NULL;
	return evdev_names[code];
}

uint32_t tl_key_char(uint32_t code, unsigned mods,
	const struct tl_key_modes *m)
{
	if (m != NULL && m->num_lock) {
		switch (code) {
		case TL_KEY_KP0: return '0';
		case TL_KEY_KP1: return '1';
		case TL_KEY_KP2: return '2';
		case TL_KEY_KP3: return '3';
		case TL_KEY_KP4: return '4';
		case TL_KEY_KP5: return '5';
		case TL_KEY_KP6: return '6';
		case TL_KEY_KP7: return '7';
		case TL_KEY_KP8: return '8';
		case TL_KEY_KP9: return '9';
		case TL_KEY_KPDOT: return '.';
		default: break;
		}
	}
	return tl_keymap_us(code, (mods & TL_MOD_SHIFT) != 0,
		m != NULL && m->caps_lock);
}

struct strbuf {
	char *buf;
	size_t cap;
	size_t len;
};

static void sb_putc(struct strbuf *s, char c)
{
	if (s->len + 1 < s->cap)
		s->buf[s->len++] = c;
}

static void sb_puts(struct strbuf *s, const char *str)
{
	while (*str != '\0')
		sb_putc(s, *str++);
}

static void sb_uint(struct strbuf *s, uint32_t v)
{
	char tmp[16];
	int n = snprintf(tmp, sizeof(tmp), "%u", v);
	if (n > 0)
		sb_puts(s, tmp);
}

static void sb_hex2(struct strbuf *s, unsigned char b)
{
	static const char hex[] = "0123456789abcdef";
	sb_putc(s, hex[(b >> 4) & 0xf]);
	sb_putc(s, hex[b & 0xf]);
}

static const char *event_name(enum tl_key_event ev)
{
	switch (ev) {
	case TL_KEY_PRESS:   return "press";
	case TL_KEY_REPEAT:  return "repeat";
	case TL_KEY_RELEASE: return "release";
	default:             return "unknown";
	}
}

size_t tl_key_trace_format(char *buf, size_t cap, uint32_t code,
	unsigned mods, enum tl_key_event ev, const struct tl_key_modes *m,
	const char *pty_bytes, size_t pty_len, const char *note)
{
	if (cap == 0)
		return 0;

	struct strbuf s = { buf, cap, 0 };
	const char *name = tl_evdev_name(code);

	sb_uint(&s, code);
	sb_putc(&s, ' ');
	sb_puts(&s, name != NULL ? name : "UNKNOWN");
	sb_putc(&s, ' ');
	sb_puts(&s, event_name(ev));

	/* Held modifiers. */
	sb_puts(&s, " mods=");
	static const struct { unsigned bit; const char *name; } mod_list[] = {
		{ TL_MOD_SHIFT, "Shift" },
		{ TL_MOD_ALT,   "Alt" },
		{ TL_MOD_CTRL,  "Ctrl" },
		{ TL_MOD_META,  "Meta" },
	};
	bool any = false;
	for (size_t i = 0; i < sizeof(mod_list) / sizeof(mod_list[0]); i++) {
		if (mods & mod_list[i].bit) {
			if (any)
				sb_putc(&s, '+');
			sb_puts(&s, mod_list[i].name);
			any = true;
		}
	}
	if (!any)
		sb_putc(&s, '-');

	/* Locks. */
	sb_puts(&s, " locks=");
	bool caps = m != NULL && m->caps_lock;
	bool num = m != NULL && m->num_lock;
	if (caps && num)
		sb_puts(&s, "Caps+Num");
	else if (caps)
		sb_puts(&s, "Caps");
	else if (num)
		sb_puts(&s, "Num");
	else
		sb_putc(&s, '-');

	/* Layout character. */
	sb_puts(&s, " char=");
	uint32_t ch = tl_key_char(code, mods, m);
	if (ch == 0) {
		sb_putc(&s, '-');
	} else if (ch == '\'') {
		sb_puts(&s, "'\\''");
	} else if (ch == '\\') {
		sb_puts(&s, "'\\\\'");
	} else if (ch >= 0x20 && ch <= 0x7e) {
		sb_putc(&s, '\'');
		sb_putc(&s, (char)ch);
		sb_putc(&s, '\'');
	} else {
		char tmp[16];
		snprintf(tmp, sizeof(tmp), "U+%04X", ch);
		sb_puts(&s, tmp);
	}

	/* Kitty flags when active. */
	unsigned kflags = m != NULL ? (m->kitty_flags & TL_KITTY_MASK) : 0;
	if (kflags != 0) {
		sb_puts(&s, " kitty=");
		sb_uint(&s, kflags);
	}

	/* Exact bytes written to the pty. */
	sb_puts(&s, " pty=");
	if (pty_bytes != NULL && pty_len > 0) {
		sb_putc(&s, '"');
		for (size_t i = 0; i < pty_len; i++) {
			unsigned char b = (unsigned char)pty_bytes[i];
			if (b == 0x1b) {
				sb_puts(&s, "ESC");
			} else if (b == '\r') {
				sb_puts(&s, "\\r");
			} else if (b == '\n') {
				sb_puts(&s, "\\n");
			} else if (b == '\t') {
				sb_puts(&s, "\\t");
			} else if (b == '"') {
				sb_puts(&s, "\\\"");
			} else if (b == '\\') {
				sb_puts(&s, "\\\\");
			} else if (b >= 0x20 && b <= 0x7e) {
				sb_putc(&s, (char)b);
			} else {
				sb_puts(&s, "\\x");
				sb_hex2(&s, b);
			}
		}
		sb_puts(&s, "\" [");
		for (size_t i = 0; i < pty_len; i++) {
			if (i > 0)
				sb_putc(&s, ' ');
			sb_hex2(&s, (unsigned char)pty_bytes[i]);
		}
		sb_putc(&s, ']');
	} else {
		sb_puts(&s, "(none)");
	}

	if (note != NULL && note[0] != '\0') {
		sb_puts(&s, " [");
		sb_puts(&s, note);
		sb_putc(&s, ']');
	}

	s.buf[s.len] = '\0';
	return s.len;
}
