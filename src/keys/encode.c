/* Key encoding: legacy xterm encoding and the kitty keyboard protocol
 * (progressive enhancement flags 1..16, CSI u, release/repeat events,
 * alternate keys, associated text).
 *
 * Legacy mode follows xterm's ctlseqs (foot's input.c and keymap.h were
 * read for details). Kitty mode follows kitty's docs/keyboard-protocol.rst
 * and foot's kitty_kbd_protocol. */
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

/* Kitty PUA functional codepoints for keypad keys. Sets *text_out to the
 * printable character when the keypad key produces text, or 0 otherwise. */
static uint32_t kitty_keypad_key(uint32_t code, bool num_lock,
	uint32_t *text_out)
{
	*text_out = 0;
	switch (code) {
	case TL_KEY_KPSLASH:    *text_out = '/'; return 57410;
	case TL_KEY_KPASTERISK: *text_out = '*'; return 57411;
	case TL_KEY_KPMINUS:    *text_out = '-'; return 57412;
	case TL_KEY_KPPLUS:     *text_out = '+'; return 57413;
	case TL_KEY_KPENTER:                     return 57414;
	default: break;
	}
	if (num_lock) {
		switch (code) {
		case TL_KEY_KP0:   *text_out = '0'; return 57399;
		case TL_KEY_KP1:   *text_out = '1'; return 57400;
		case TL_KEY_KP2:   *text_out = '2'; return 57401;
		case TL_KEY_KP3:   *text_out = '3'; return 57402;
		case TL_KEY_KP4:   *text_out = '4'; return 57403;
		case TL_KEY_KP5:   *text_out = '5'; return 57404;
		case TL_KEY_KP6:   *text_out = '6'; return 57405;
		case TL_KEY_KP7:   *text_out = '7'; return 57406;
		case TL_KEY_KP8:   *text_out = '8'; return 57407;
		case TL_KEY_KP9:   *text_out = '9'; return 57408;
		case TL_KEY_KPDOT: *text_out = '.'; return 57409;
		default: return 0;
		}
	}
	switch (code) {
	case TL_KEY_KP4:   return 57417; /* KP_LEFT */
	case TL_KEY_KP6:   return 57418; /* KP_RIGHT */
	case TL_KEY_KP8:   return 57419; /* KP_UP */
	case TL_KEY_KP2:   return 57420; /* KP_DOWN */
	case TL_KEY_KP9:   return 57421; /* KP_PAGE_UP */
	case TL_KEY_KP3:   return 57422; /* KP_PAGE_DOWN */
	case TL_KEY_KP7:   return 57423; /* KP_HOME */
	case TL_KEY_KP1:   return 57424; /* KP_END */
	case TL_KEY_KP0:   return 57425; /* KP_INSERT */
	case TL_KEY_KPDOT: return 57426; /* KP_DELETE */
	case TL_KEY_KP5:   return 57427; /* KP_BEGIN */
	default: return 0;
	}
}

/* Kitty PUA functional codepoints for modifier, lock, and system keys. */
static uint32_t kitty_mod_or_sys_key(uint32_t code, bool *is_mod)
{
	*is_mod = true;
	switch (code) {
	case TL_KEY_CAPSLOCK:   return 57358;
	case TL_KEY_SCROLLLOCK: return 57359;
	case TL_KEY_NUMLOCK:    return 57360;
	case TL_KEY_LEFTSHIFT:  return 57441;
	case TL_KEY_LEFTCTRL:   return 57442;
	case TL_KEY_LEFTALT:    return 57443;
	case TL_KEY_LEFTMETA:   return 57444;
	case TL_KEY_RIGHTSHIFT: return 57447;
	case TL_KEY_RIGHTCTRL:  return 57448;
	case TL_KEY_RIGHTALT:   return 57449;
	case TL_KEY_RIGHTMETA:  return 57450;
	case TL_KEY_SYSRQ:      *is_mod = false; return 57361;
	case TL_KEY_COMPOSE:    *is_mod = false; return 57363;
	default:                *is_mod = false; return 0;
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

size_t tl_utf8_encode(uint32_t cp, char out[4])
{
	if (cp == 0 || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu))
		return 0;
	if (cp < 0x80u) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800u) {
		out[0] = (char)(0xc0u | (cp >> 6));
		out[1] = (char)(0x80u | (cp & 0x3fu));
		return 2;
	}
	if (cp < 0x10000u) {
		out[0] = (char)(0xe0u | (cp >> 12));
		out[1] = (char)(0x80u | ((cp >> 6) & 0x3fu));
		out[2] = (char)(0x80u | (cp & 0x3fu));
		return 3;
	}
	out[0] = (char)(0xf0u | (cp >> 18));
	out[1] = (char)(0x80u | ((cp >> 12) & 0x3fu));
	out[2] = (char)(0x80u | ((cp >> 6) & 0x3fu));
	out[3] = (char)(0x80u | (cp & 0x3fu));
	return 4;
}

size_t tl_key_encode_text(uint32_t cp, char out[TL_KEY_SEQ_MAX])
{
	char utf8[4];
	size_t n = tl_utf8_encode(cp, utf8);
	if (n == 0)
		return 0;
	memcpy(out, utf8, n);
	return n;
}

static size_t fmt_csi_u(char out[TL_KEY_SEQ_MAX], uint32_t key,
	uint32_t shifted, unsigned kmods, enum tl_key_event ev, bool report_ev,
	uint32_t text)
{
	char key_part[32];
	char mod_part[24];
	char txt_part[16];
	bool need_ev = report_ev && ev != TL_KEY_PRESS;
	bool need_mod = kmods != 0 || need_ev;
	bool need_text = text != 0;

	if (shifted != 0)
		snprintf(key_part, sizeof(key_part), "%u:%u", key, shifted);
	else
		snprintf(key_part, sizeof(key_part), "%u", key);

	if (need_ev)
		snprintf(mod_part, sizeof(mod_part), "%u:%d", 1u + kmods,
			(int)ev);
	else if (need_mod)
		snprintf(mod_part, sizeof(mod_part), "%u", 1u + kmods);
	else
		mod_part[0] = '\0';

	if (need_text)
		snprintf(txt_part, sizeof(txt_part), "%u", text);
	else
		txt_part[0] = '\0';

	int n;
	if (need_text)
		n = snprintf(out, TL_KEY_SEQ_MAX, "\033[%s;%s;%su", key_part,
			mod_part, txt_part);
	else if (need_mod)
		n = snprintf(out, TL_KEY_SEQ_MAX, "\033[%s;%su", key_part,
			mod_part);
	else
		n = snprintf(out, TL_KEY_SEQ_MAX, "\033[%su", key_part);
	return n > 0 && (size_t)n < TL_KEY_SEQ_MAX ? (size_t)n : 0;
}

static size_t fmt_csi_letter(char out[TL_KEY_SEQ_MAX], int letter,
	bool use_ss3, unsigned kmods, enum tl_key_event ev, bool report_ev)
{
	bool need_ev = report_ev && ev != TL_KEY_PRESS;
	int n;
	if (need_ev)
		n = snprintf(out, TL_KEY_SEQ_MAX, "\033[1;%u:%d%c", 1u + kmods,
			(int)ev, letter);
	else if (kmods != 0)
		n = snprintf(out, TL_KEY_SEQ_MAX, "\033[1;%u%c", 1u + kmods,
			letter);
	else
		n = snprintf(out, TL_KEY_SEQ_MAX, "\033%c%c",
			use_ss3 ? 'O' : '[', letter);
	return n > 0 && (size_t)n < TL_KEY_SEQ_MAX ? (size_t)n : 0;
}

static size_t fmt_tilde(char out[TL_KEY_SEQ_MAX], int num, unsigned kmods,
	enum tl_key_event ev, bool report_ev)
{
	bool need_ev = report_ev && ev != TL_KEY_PRESS;
	int n;
	if (need_ev)
		n = snprintf(out, TL_KEY_SEQ_MAX, "\033[%d;%u:%d~", num,
			1u + kmods, (int)ev);
	else if (kmods != 0)
		n = snprintf(out, TL_KEY_SEQ_MAX, "\033[%d;%u~", num,
			1u + kmods);
	else
		n = snprintf(out, TL_KEY_SEQ_MAX, "\033[%d~", num);
	return n > 0 && (size_t)n < TL_KEY_SEQ_MAX ? (size_t)n : 0;
}

static size_t encode_legacy(uint32_t code, unsigned mods,
	const struct tl_key_modes *m, char out[TL_KEY_SEQ_MAX])
{
	bool shift = (mods & TL_MOD_SHIFT) != 0;
	bool alt = (mods & TL_MOD_ALT) != 0;
	bool ctrl = (mods & TL_MOD_CTRL) != 0;
	unsigned kmods = mods & 15u;

	if (tl_key_is_modifier(code))
		return 0;

	struct special sp = special(code, m->num_lock);
	switch (sp.kind) {
	case CSI_LETTER:
		return fmt_csi_letter(out, sp.value,
			m->app_cursor && sp.value != 'E', kmods, TL_KEY_PRESS,
			false);
	case SS3_LETTER:
		return fmt_csi_letter(out, sp.value, true, kmods, TL_KEY_PRESS,
			false);
	case TILDE:
		return fmt_tilde(out, sp.value, kmods, TL_KEY_PRESS, false);
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
		ch = tl_layout_lookup(m->layout, code, mods, m->caps_lock);
	if (ch == 0 || (ch & TL_KEY_DEAD_BIT) != 0)
		return 0;
	if (ctrl) {
		ch = ctrl_char(ch);
		if (ch == 0) {
			char z = '\0';
			return put(out, alt, &z, 1);
		}
	}
	/* Meta alone changes nothing: legacy encoding has no way to say it. */
	char utf8[4];
	size_t ulen = tl_utf8_encode(ch, utf8);
	if (ulen == 0)
		return 0;
	return put(out, alt, utf8, ulen);
}

size_t tl_key_encode_event(uint32_t code, unsigned mods, enum tl_key_event ev,
	const struct tl_key_modes *m, char out[TL_KEY_SEQ_MAX])
{
	unsigned flags = m->kitty_flags & TL_KITTY_MASK;
	if (flags == 0) {
		if (ev == TL_KEY_RELEASE)
			return 0;
		return encode_legacy(code, mods, m, out);
	}

	bool disambig = (flags & TL_KITTY_DISAMBIGUATE) != 0;
	bool report_ev = (flags & TL_KITTY_EVENTS) != 0;
	bool report_alt = (flags & TL_KITTY_ALTERNATE) != 0;
	bool report_all = (flags & TL_KITTY_ALL_KEYS) != 0;
	bool report_text = (flags & TL_KITTY_TEXT) != 0;

	if (ev == TL_KEY_RELEASE && !report_ev)
		return 0;

	unsigned kmods = (mods & 15u)
		| (m->caps_lock ? 64u : 0u)
		| (m->num_lock ? 128u : 0u);
	bool any_mod = (mods & 15u) != 0;
	bool non_shift_mod = (mods & (TL_MOD_ALT | TL_MOD_CTRL | TL_MOD_META)) != 0;

	/* Modifier, lock, and system functional keys. */
	bool is_mod = false;
	uint32_t mod_cp = kitty_mod_or_sys_key(code, &is_mod);
	if (mod_cp != 0) {
		if (is_mod) {
			if (!report_all || ev == TL_KEY_REPEAT)
				return 0;
		} else if (!disambig && !report_all) {
			return 0;
		}
		return fmt_csi_u(out, mod_cp, 0, kmods, ev, report_ev, 0);
	}

	/* Keypad keys: distinct PUA functional codes under disambig/all_keys. */
	uint32_t kp_text = 0;
	uint32_t kp_cp = kitty_keypad_key(code, m->num_lock, &kp_text);
	if (kp_cp != 0 && (disambig || report_all)) {
		uint32_t text = (report_text && ev != TL_KEY_RELEASE &&
			!non_shift_mod) ? kp_text : 0;
		return fmt_csi_u(out, kp_cp, 0, kmods, ev, report_ev, text);
	}

	/* Cursor, editing, and function keys (plus legacy keypad nav if !disambig). */
	struct special sp = special(code, m->num_lock);
	if (sp.kind != NONE) {
		if (code == TL_KEY_F3 && (disambig || report_all))
			return fmt_tilde(out, 13, kmods, ev, report_ev);
		if (sp.kind == TILDE)
			return fmt_tilde(out, sp.value, kmods, ev, report_ev);
		bool use_ss3 = false;
		if (!(disambig || report_all))
			use_ss3 = (sp.kind == SS3_LETTER) ||
				(m->app_cursor && sp.value != 'E');
		return fmt_csi_letter(out, sp.value, use_ss3, kmods, ev,
			report_ev);
	}

	/* Escape, Enter, Tab, Backspace. */
	uint32_t ctrl_cp = 0;
	switch (code) {
	case TL_KEY_ESC:       ctrl_cp = 27;  break;
	case TL_KEY_ENTER:     ctrl_cp = 13;  break;
	case TL_KEY_TAB:       ctrl_cp = 9;   break;
	case TL_KEY_BACKSPACE: ctrl_cp = 127; break;
	default: break;
	}
	if (ctrl_cp != 0) {
		bool use_csi = report_all ||
			(disambig && (ctrl_cp == 27 || any_mod));
		if (use_csi)
			return fmt_csi_u(out, ctrl_cp, 0, kmods, ev, report_ev,
				0);
		if (ev == TL_KEY_RELEASE)
			return 0;
		return encode_legacy(code, mods, m, out);
	}

	/* Printable text keys. */
	uint32_t unshifted = tl_layout_lookup(m->layout, code, 0, false);
	uint32_t cur_sym = tl_layout_lookup(m->layout, code, mods, m->caps_lock);
	if ((cur_sym & TL_KEY_DEAD_BIT) != 0)
		return 0;
	if (unshifted == 0 || (unshifted & TL_KEY_DEAD_BIT) != 0) {
		if (cur_sym == 0) {
			if (ev == TL_KEY_RELEASE)
				return 0;
			return encode_legacy(code, mods, m, out);
		}
		unshifted = cur_sym;
	}

	bool use_csi_u = report_all || (disambig && non_shift_mod);
	if (!use_csi_u) {
		if (ev == TL_KEY_RELEASE)
			return 0;
		return encode_legacy(code, mods, m, out);
	}

	uint32_t shifted = tl_layout_lookup(m->layout, code, TL_MOD_SHIFT, false);
	if ((shifted & TL_KEY_DEAD_BIT) != 0)
		shifted = 0;
	uint32_t alt_shifted = (report_alt && (mods & TL_MOD_SHIFT) &&
		shifted != 0 && shifted != unshifted) ? shifted : 0;
	uint32_t text_cp = cur_sym;
	uint32_t assoc = (report_text && ev != TL_KEY_RELEASE &&
		!non_shift_mod && text_cp >= 32 && text_cp != 127) ? text_cp : 0;
	return fmt_csi_u(out, unshifted, alt_shifted, kmods, ev, report_ev,
		assoc);
}

size_t tl_key_encode(uint32_t code, unsigned mods,
	const struct tl_key_modes *m, char out[TL_KEY_SEQ_MAX])
{
	return tl_key_encode_event(code, mods, TL_KEY_PRESS, m, out);
}
