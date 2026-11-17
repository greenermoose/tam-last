/* The legacy xterm key encoding: what every terminal program expects
 * today. Text keys send their character; Ctrl folds letters into C0
 * controls; Alt puts ESC in front; cursor, editing and function keys send
 * CSI or SS3 sequences, with a modifier parameter of 1 + Shift(1) +
 * Alt(2) + Ctrl(4) + Meta(8). Many key combinations come out as the same
 * bytes here (Ctrl+I and Tab, for one); the kitty keyboard protocol (P2)
 * is how a program can tell them apart. Written from xterm's ctlseqs;
 * foot's input.c and keymap.h were read for the details. */
#include "tam-last/keys.h"

#include <stdio.h>
#include <string.h>

enum kind { NONE, CSI_LETTER, SS3_LETTER, TILDE };

struct special {
	enum kind kind;
	int value; /* the letter, or the number before ~ */
};

static struct special special(uint32_t code, bool num_lock)
{
	switch (code) {
	case TL_KEY_UP: return (struct special){ CSI_LETTER, 'A' };
	case TL_KEY_DOWN: return (struct special){ CSI_LETTER, 'B' };
	case TL_KEY_RIGHT: return (struct special){ CSI_LETTER, 'C' };
	case TL_KEY_LEFT: return (struct special){ CSI_LETTER, 'D' };
	case TL_KEY_HOME: return (struct special){ CSI_LETTER, 'H' };
	case TL_KEY_END: return (struct special){ CSI_LETTER, 'F' };
	case TL_KEY_INSERT: return (struct special){ TILDE, 2 };
	case TL_KEY_DELETE: return (struct special){ TILDE, 3 };
	case TL_KEY_PAGEUP: return (struct special){ TILDE, 5 };
	case TL_KEY_PAGEDOWN: return (struct special){ TILDE, 6 };
	case TL_KEY_F1: return (struct special){ SS3_LETTER, 'P' };
	case TL_KEY_F2: return (struct special){ SS3_LETTER, 'Q' };
	case TL_KEY_F3: return (struct special){ SS3_LETTER, 'R' };
	case TL_KEY_F4: return (struct special){ SS3_LETTER, 'S' };
	case TL_KEY_F5: return (struct special){ TILDE, 15 };
	case TL_KEY_F6: return (struct special){ TILDE, 17 };
	case TL_KEY_F7: return (struct special){ TILDE, 18 };
	case TL_KEY_F8: return (struct special){ TILDE, 19 };
	case TL_KEY_F9: return (struct special){ TILDE, 20 };
	case TL_KEY_F10: return (struct special){ TILDE, 21 };
	case TL_KEY_F11: return (struct special){ TILDE, 23 };
	case TL_KEY_F12: return (struct special){ TILDE, 24 };
	default: break;
	}
	if (num_lock)
		return (struct special){ NONE, 0 };
	/* The keypad without Num Lock is a second set of cursor keys. */
	switch (code) {
	case TL_KEY_KP8: return special(TL_KEY_UP, false);
	case TL_KEY_KP2: return special(TL_KEY_DOWN, false);
	case TL_KEY_KP6: return special(TL_KEY_RIGHT, false);
	case TL_KEY_KP4: return special(TL_KEY_LEFT, false);
	case TL_KEY_KP7: return special(TL_KEY_HOME, false);
	case TL_KEY_KP1: return special(TL_KEY_END, false);
	case TL_KEY_KP9: return special(TL_KEY_PAGEUP, false);
	case TL_KEY_KP3: return special(TL_KEY_PAGEDOWN, false);
	case TL_KEY_KP0: return special(TL_KEY_INSERT, false);
	case TL_KEY_KPDOT: return special(TL_KEY_DELETE, false);
	case TL_KEY_KP5: return (struct special){ CSI_LETTER, 'E' };
	default: return (struct special){ NONE, 0 };
	}
}

static uint32_t keypad_digit(uint32_t code)
{
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
	default: return 0;
	}
}

/* What Ctrl does to a character: letters and @[\]^_ become C0 controls,
 * and a few digits and punctuation follow xterm's VT220 table. */
static uint32_t ctrl_char(uint32_t ch)
{
	if ((ch >= '@' && ch <= '_') || (ch >= 'a' && ch <= 'z'))
		return ch & 0x1f;
	switch (ch) {
	case ' ': case '2': return 0x00;
	case '3': return 0x1b;
	case '4': return 0x1c;
	case '5': return 0x1d;
	case '6': return 0x1e;
	case '7': case '/': return 0x1f;
	case '8': case '?': return 0x7f;
	default: return ch;
	}
}

static size_t put(char out[TL_KEY_SEQ_MAX], bool alt, const char *s,
	size_t n)
{
	size_t len = 0;
	if (alt)
		out[len++] = '\033';
	memcpy(out + len, s, n);
	return len + n;
}

size_t tl_key_encode(uint32_t code, unsigned mods,
	const struct tl_key_modes *m, char out[TL_KEY_SEQ_MAX])
{
	bool shift = mods & TL_MOD_SHIFT, alt = mods & TL_MOD_ALT;
	bool ctrl = mods & TL_MOD_CTRL;
	int param = 1 + (int)(mods & 15u);
	int n;

	if (tl_key_is_modifier(code))
		return 0;

	struct special sp = special(code, m->num_lock);
	switch (sp.kind) {
	case CSI_LETTER:
		if (param > 1)
			n = snprintf(out, TL_KEY_SEQ_MAX, "\033[1;%d%c", param,
				sp.value);
		else
			n = snprintf(out, TL_KEY_SEQ_MAX, "\033%c%c",
				m->app_cursor && sp.value != 'E' ? 'O' : '[', sp.value);
		return (size_t)n;
	case SS3_LETTER:
		if (param > 1)
			n = snprintf(out, TL_KEY_SEQ_MAX, "\033[1;%d%c", param,
				sp.value);
		else
			n = snprintf(out, TL_KEY_SEQ_MAX, "\033O%c", sp.value);
		return (size_t)n;
	case TILDE:
		if (param > 1)
			n = snprintf(out, TL_KEY_SEQ_MAX, "\033[%d;%d~", sp.value,
				param);
		else
			n = snprintf(out, TL_KEY_SEQ_MAX, "\033[%d~", sp.value);
		return (size_t)n;
	case NONE:
		break;
	}

	switch (code) {
	case TL_KEY_ENTER:
	case TL_KEY_KPENTER:
		return put(out, alt, "\r", 1);
	case TL_KEY_TAB:
		if (shift)
			return put(out, alt, "\033[Z", 3);
		return put(out, alt, "\t", 1);
	case TL_KEY_BACKSPACE:
		return put(out, alt, ctrl ? "\b" : "\177", 1);
	case TL_KEY_ESC:
		return put(out, alt, "\033", 1);
	default:
		break;
	}

	uint32_t ch = m->num_lock ? keypad_digit(code) : 0;
	if (ch == 0)
		ch = tl_keymap_us(code, shift, m->caps_lock);
	if (ch == 0)
		return 0;
	if (ctrl)
		ch = ctrl_char(ch);
	/* The US layout is ASCII, so one byte. Meta alone changes nothing:
	 * legacy encoding has no way to say it. */
	char c = (char)ch;
	return put(out, alt, &c, 1);
}
