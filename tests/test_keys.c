/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The key path inside tam-last: evdev codes, keyboard state, the US
 * layout, the bytes sent, and a round trip through libtam's tam_key, the
 * decoder our own programs read keys with. */
#include <stdio.h>
#include <string.h>

#include <tam_key.h>

#include "check.h"
#include "tam-last/keys.h"

#if defined(__has_include)
#if __has_include(<linux/input-event-codes.h>)
#include <linux/input-event-codes.h>
#define HAVE_KERNEL_CODES 1
#endif
#endif

static const struct tl_key_modes plain = { false, false, false, false, 0, NULL };

static int sends_ev(uint32_t code, unsigned mods, enum tl_key_event ev,
	const struct tl_key_modes *m, const char *want, size_t want_len)
{
	char out[TL_KEY_SEQ_MAX];
	size_t n = tl_key_encode_event(code, mods, ev, m, out);
	if (n != want_len || memcmp(out, want, n) != 0) {
		fprintf(stderr, "  key %u mods %u ev %d: got %zu bytes:", code,
			mods, (int)ev, n);
		for (size_t i = 0; i < n; i++)
			fprintf(stderr, " %02x", (unsigned char)out[i]);
		fputc('\n', stderr);
		return 0;
	}
	return 1;
}
#define SENDS_EV(code, mods, ev, m, s) \
	CHECK(sends_ev(code, mods, ev, m, s, sizeof(s) - 1))

/* Self-contained kitty keyboard protocol decoder for round-trip checks. */
struct kitty_decoded {
	uint32_t key;
	uint32_t shifted;
	unsigned mods;      /* 0-based kitty modifier bitmask */
	unsigned ev;        /* 1 = press, 2 = repeat, 3 = release */
	uint32_t text;
	char final;
};

static bool kitty_decode(const char *s, size_t n, struct kitty_decoded *d)
{
	memset(d, 0, sizeof(*d));
	d->ev = 1;
	if (n < 3 || s[0] != '\033' || s[1] != '[')
		return false;
	d->final = s[n - 1];
	size_t i = 2, end = n - 1;
	int field = 0, sub = 0;
	uint32_t vals[3][3] = { { 0 } };
	bool has[3][3] = { { false } };
	while (i < end) {
		char c = s[i++];
		if (c == ';') {
			field++;
			sub = 0;
			if (field >= 3)
				return false;
		} else if (c == ':') {
			sub++;
			if (sub >= 3)
				return false;
		} else if (c >= '0' && c <= '9') {
			vals[field][sub] = vals[field][sub] * 10u + (uint32_t)(c - '0');
			has[field][sub] = true;
		} else {
			return false;
		}
	}
	d->key = vals[0][0];
	d->shifted = has[0][1] ? vals[0][1] : 0;
	if (has[1][0] && vals[1][0] >= 1)
		d->mods = vals[1][0] - 1u;
	if (has[1][1])
		d->ev = vals[1][1];
	if (has[2][0])
		d->text = vals[2][0];
	return true;
}

static bool kitty_rt(uint32_t code, unsigned mods, enum tl_key_event ev,
	const struct tl_key_modes *m, struct kitty_decoded *out_d)
{
	char seq[TL_KEY_SEQ_MAX];
	size_t n = tl_key_encode_event(code, mods, ev, m, seq);
	return kitty_decode(seq, n, out_d);
}

static int sends(uint32_t code, unsigned mods, const struct tl_key_modes *m,
	const char *want, size_t want_len)
{
	char out[TL_KEY_SEQ_MAX];
	size_t n = tl_key_encode(code, mods, m, out);
	if (n != want_len || memcmp(out, want, n) != 0) {
		fprintf(stderr, "  key %u mods %u: got %zu bytes:", code, mods, n);
		for (size_t i = 0; i < n; i++)
			fprintf(stderr, " %02x", (unsigned char)out[i]);
		fputc('\n', stderr);
		return 0;
	}
	return 1;
}
#define SENDS(code, mods, m, s) CHECK(sends(code, mods, m, s, sizeof(s) - 1))

/* Encode, then decode with tam_key; compare the event. */
static int round_trip(uint32_t code, unsigned mods, tam_key_type_t type,
	uint32_t cp, unsigned want_mods)
{
	char out[TL_KEY_SEQ_MAX];
	size_t n = tl_key_encode(code, mods, &plain, out);
	tam_key_parser_t p;
	tam_key_event_t ev;
	tam_key_init(&p);
	tam_key_feed(&p, out, n);
	int r = tam_key_next(&p, &ev);
	if (r == 0 && tam_key_timeout(&p, &ev) == 1)
		r = 1; /* a lone ESC */
	int ok = r == 1 && ev.type == type && ev.mods == want_mods &&
		(type != TAM_KEY_CHAR || ev.codepoint == cp) &&
		tam_key_pending(&p) == 0;
	if (!ok)
		fprintf(stderr, "  round trip of key %u mods %u: type %d cp %u "
			"mods %u\n", code, mods, ev.type, ev.codepoint, ev.mods);
	return ok;
}

int main(void)
{
#ifdef HAVE_KERNEL_CODES
	/* Our copy of the evdev numbers matches the kernel's. */
	CHECK(TL_KEY_ESC == KEY_ESC && TL_KEY_A == KEY_A && TL_KEY_Z == KEY_Z);
	CHECK(TL_KEY_ENTER == KEY_ENTER && TL_KEY_SPACE == KEY_SPACE);
	CHECK(TL_KEY_LEFTSHIFT == KEY_LEFTSHIFT && TL_KEY_RIGHTALT == KEY_RIGHTALT);
	CHECK(TL_KEY_F11 == KEY_F11 && TL_KEY_F12 == KEY_F12);
	CHECK(TL_KEY_KPENTER == KEY_KPENTER && TL_KEY_KPDOT == KEY_KPDOT);
	CHECK(TL_KEY_HOME == KEY_HOME && TL_KEY_DELETE == KEY_DELETE);
	CHECK(TL_KEY_LEFTMETA == KEY_LEFTMETA && TL_KEY_COMPOSE == KEY_COMPOSE);
	CHECK(TL_KEY_102ND == KEY_102ND && TL_KEY_GRAVE == KEY_GRAVE);
#else
	printf("test_keys: no kernel headers; evdev cross-check skipped\n");
#endif

	/* Keyboard state. */
	struct tl_kbd k;
	tl_kbd_init(&k);
	tl_kbd_key(&k, TL_KEY_LEFTSHIFT, true);
	tl_kbd_key(&k, TL_KEY_RIGHTCTRL, true);
	CHECK(tl_kbd_mods(&k) == (TL_MOD_SHIFT | TL_MOD_CTRL));
	tl_kbd_key(&k, TL_KEY_LEFTSHIFT, false);
	CHECK(tl_kbd_mods(&k) == TL_MOD_CTRL);
	tl_kbd_leave(&k);
	CHECK(tl_kbd_mods(&k) == 0);
	uint32_t held[] = { TL_KEY_LEFTALT, TL_KEY_A, 9999 };
	tl_kbd_enter(&k, held, 3);
	CHECK(tl_kbd_mods(&k) == TL_MOD_ALT);
	tl_kbd_locks(&k, TL_XKB_LOCK);
	CHECK(k.caps && !k.num);
	tl_kbd_locks(&k, TL_XKB_MOD2);
	CHECK(!k.caps && k.num);

	/* Compositor depressed modifiers (synthetic shortcuts such as
	 * Hyprland send_key_state or wtype without physical modifier keys). */
	tl_kbd_init(&k);
	tl_kbd_modifiers(&k, TL_XKB_CTRL | TL_XKB_SHIFT, 0);
	CHECK(tl_kbd_mods(&k) == (TL_MOD_CTRL | TL_MOD_SHIFT));
	/* Synthetic chord overrides physically held Meta so Meta+V / Meta+R
	 * do not leak Meta into the injected paste chord. */
	tl_kbd_key(&k, TL_KEY_LEFTMETA, true);
	tl_kbd_modifiers(&k, TL_XKB_SHIFT, 0);
	CHECK(tl_kbd_mods(&k) == TL_MOD_SHIFT);
	tl_kbd_modifiers(&k, 0, TL_XKB_LOCK);
	CHECK(tl_kbd_mods(&k) == 0 && k.caps && !k.num);
	tl_kbd_leave(&k);
	CHECK(tl_kbd_mods(&k) == 0 && !k.have_mods);

	/* The layout. */
	CHECK(tl_keymap_us(TL_KEY_A, false, false) == 'a');
	CHECK(tl_keymap_us(TL_KEY_A, true, false) == 'A');
	CHECK(tl_keymap_us(TL_KEY_A, false, true) == 'A');
	CHECK(tl_keymap_us(TL_KEY_A, true, true) == 'a');
	CHECK(tl_keymap_us(TL_KEY_1, false, true) == '1');
	CHECK(tl_keymap_us(TL_KEY_1, true, false) == '!');
	CHECK(tl_keymap_us(TL_KEY_SLASH, true, false) == '?');
	CHECK(tl_keymap_us(TL_KEY_F1, false, false) == 0);

	/* The bytes. */
	struct tl_key_modes app = { true, false, false, false, 0, NULL };
	struct tl_key_modes num = { false, false, true, false, 0, NULL };
	struct tl_key_modes caps = { false, false, false, true, 0, NULL };
	SENDS(TL_KEY_A, 0, &plain, "a");
	SENDS(TL_KEY_A, TL_MOD_SHIFT, &plain, "A");
	SENDS(TL_KEY_A, 0, &caps, "A");
	SENDS(TL_KEY_A, TL_MOD_CTRL, &plain, "\001");
	SENDS(TL_KEY_A, TL_MOD_CTRL | TL_MOD_SHIFT, &plain, "\001");
	SENDS(TL_KEY_A, TL_MOD_ALT, &plain, "\033a");
	SENDS(TL_KEY_A, TL_MOD_ALT | TL_MOD_CTRL, &plain, "\033\001");
	SENDS(TL_KEY_A, TL_MOD_META, &plain, "a");
	SENDS(TL_KEY_SPACE, TL_MOD_CTRL, &plain, "\0");
	SENDS(TL_KEY_LEFTBRACE, TL_MOD_CTRL, &plain, "\033");
	SENDS(TL_KEY_SLASH, TL_MOD_CTRL, &plain, "\037");
	SENDS(TL_KEY_UP, 0, &plain, "\033[A");
	SENDS(TL_KEY_UP, 0, &app, "\033OA");
	SENDS(TL_KEY_UP, TL_MOD_CTRL, &app, "\033[1;5A");
	SENDS(TL_KEY_LEFT, TL_MOD_SHIFT | TL_MOD_ALT, &plain, "\033[1;4D");
	SENDS(TL_KEY_HOME, 0, &plain, "\033[H");
	SENDS(TL_KEY_END, TL_MOD_META, &plain, "\033[1;9F");
	SENDS(TL_KEY_F1, 0, &plain, "\033OP");
	SENDS(TL_KEY_F1, TL_MOD_CTRL, &plain, "\033[1;5P");
	SENDS(TL_KEY_F5, TL_MOD_SHIFT, &plain, "\033[15;2~");
	SENDS(TL_KEY_F12, 0, &plain, "\033[24~");
	SENDS(TL_KEY_DELETE, 0, &plain, "\033[3~");
	SENDS(TL_KEY_PAGEUP, TL_MOD_CTRL, &plain, "\033[5;5~");
	SENDS(TL_KEY_BACKSPACE, 0, &plain, "\177");
	SENDS(TL_KEY_BACKSPACE, TL_MOD_CTRL, &plain, "\b");
	SENDS(TL_KEY_BACKSPACE, TL_MOD_ALT, &plain, "\033\177");
	SENDS(TL_KEY_ENTER, 0, &plain, "\r");
	SENDS(TL_KEY_KPENTER, TL_MOD_ALT, &plain, "\033\r");
	SENDS(TL_KEY_TAB, 0, &plain, "\t");
	SENDS(TL_KEY_TAB, TL_MOD_SHIFT, &plain, "\033[Z");
	SENDS(TL_KEY_ESC, 0, &plain, "\033");
	SENDS(TL_KEY_KP8, 0, &plain, "\033[A");
	SENDS(TL_KEY_KP8, 0, &num, "8");
	SENDS(TL_KEY_KP5, 0, &plain, "\033[E");
	SENDS(TL_KEY_KPPLUS, 0, &plain, "+");
	SENDS(TL_KEY_LEFTSHIFT, 0, &plain, "");
	SENDS(TL_KEY_CAPSLOCK, 0, &plain, "");

	/* What our programs see. */
	CHECK(round_trip(TL_KEY_A, 0, TAM_KEY_CHAR, 'a', 0));
	CHECK(round_trip(TL_KEY_A, TL_MOD_ALT, TAM_KEY_CHAR, 'a', TAM_MOD_ALT));
	CHECK(round_trip(TL_KEY_C, TL_MOD_CTRL, TAM_KEY_CHAR, 'c', TAM_MOD_CTRL));
	CHECK(round_trip(TL_KEY_UP, 0, TAM_KEY_UP, 0, 0));
	CHECK(round_trip(TL_KEY_UP, TL_MOD_CTRL | TL_MOD_SHIFT, TAM_KEY_UP, 0,
		TAM_MOD_CTRL | TAM_MOD_SHIFT));
	CHECK(round_trip(TL_KEY_DELETE, TL_MOD_ALT, TAM_KEY_DELETE, 0, TAM_MOD_ALT));
	CHECK(round_trip(TL_KEY_F1, 0, TAM_KEY_F1, 0, 0));
	CHECK(round_trip(TL_KEY_F12, TL_MOD_SHIFT, TAM_KEY_F12, 0, TAM_MOD_SHIFT));
	CHECK(round_trip(TL_KEY_PAGEDOWN, 0, TAM_KEY_PAGE_DOWN, 0, 0));
	CHECK(round_trip(TL_KEY_BACKSPACE, 0, TAM_KEY_BACKSPACE, 0, 0));
	CHECK(round_trip(TL_KEY_ENTER, 0, TAM_KEY_ENTER, 0, 0));
	CHECK(round_trip(TL_KEY_TAB, TL_MOD_SHIFT, TAM_KEY_TAB, 0, TAM_MOD_SHIFT));
	CHECK(round_trip(TL_KEY_ESC, 0, TAM_KEY_ESC, 0, 0));

	/* What the legacy encoding loses: these presses reach the program as
	 * a different key. The kitty keyboard protocol (P2) fixes them. */
	CHECK(round_trip(TL_KEY_H, TL_MOD_CTRL, TAM_KEY_BACKSPACE, 0, 0));
	CHECK(round_trip(TL_KEY_I, TL_MOD_CTRL, TAM_KEY_TAB, 0, 0));
	CHECK(round_trip(TL_KEY_M, TL_MOD_CTRL, TAM_KEY_ENTER, 0, 0));
	CHECK(round_trip(TL_KEY_A, TL_MOD_SHIFT, TAM_KEY_CHAR, 'A', 0));
	CHECK(round_trip(TL_KEY_A, TL_MOD_CTRL | TL_MOD_SHIFT, TAM_KEY_CHAR, 'a',
		TAM_MOD_CTRL));
	CHECK(round_trip(TL_KEY_BACKSPACE, TL_MOD_CTRL, TAM_KEY_BACKSPACE, 0, 0));

	/* Kitty keyboard protocol (0.1.0): flag 1 (disambiguate). */
	struct tl_key_modes k_dis = { false, false, false, false,
		TL_KITTY_DISAMBIGUATE, NULL };
	SENDS(TL_KEY_H, TL_MOD_CTRL, &k_dis, "\033[104;5u");
	SENDS(TL_KEY_I, TL_MOD_CTRL, &k_dis, "\033[105;5u");
	SENDS(TL_KEY_M, TL_MOD_CTRL, &k_dis, "\033[109;5u");
	SENDS(TL_KEY_A, TL_MOD_CTRL, &k_dis, "\033[97;5u");
	SENDS(TL_KEY_A, TL_MOD_CTRL | TL_MOD_SHIFT, &k_dis, "\033[97;6u");
	SENDS(TL_KEY_BACKSPACE, TL_MOD_CTRL, &k_dis, "\033[127;5u");
	SENDS(TL_KEY_LEFTBRACE, TL_MOD_CTRL, &k_dis, "\033[91;5u");
	SENDS(TL_KEY_ESC, 0, &k_dis, "\033[27u");
	SENDS(TL_KEY_A, TL_MOD_META, &k_dis, "\033[97;9u");
	SENDS(TL_KEY_TAB, TL_MOD_SHIFT, &k_dis, "\033[9;2u");
	SENDS(TL_KEY_ENTER, TL_MOD_SHIFT, &k_dis, "\033[13;2u");
	/* Plain text and unmodified Enter/Tab/Backspace stay legacy under flag 1. */
	SENDS(TL_KEY_A, 0, &k_dis, "a");
	SENDS(TL_KEY_A, TL_MOD_SHIFT, &k_dis, "A");
	SENDS(TL_KEY_ENTER, 0, &k_dis, "\r");
	SENDS(TL_KEY_TAB, 0, &k_dis, "\t");
	SENDS(TL_KEY_BACKSPACE, 0, &k_dis, "\177");
	/* F1-F4 and cursor keys under disambiguate (F3 avoids CPR collision). */
	struct tl_key_modes k_dis_app = { true, false, false, false,
		TL_KITTY_DISAMBIGUATE, NULL };
	SENDS(TL_KEY_UP, 0, &k_dis_app, "\033[A");
	SENDS(TL_KEY_F1, 0, &k_dis, "\033[P");
	SENDS(TL_KEY_F2, 0, &k_dis, "\033[Q");
	SENDS(TL_KEY_F3, 0, &k_dis, "\033[13~");
	SENDS(TL_KEY_F3, TL_MOD_SHIFT, &k_dis, "\033[13;2~");
	SENDS(TL_KEY_F4, 0, &k_dis, "\033[S");
	/* Keypad keys get distinct PUA codes under disambiguate. */
	struct tl_key_modes k_dis_num = { false, false, true, false,
		TL_KITTY_DISAMBIGUATE, NULL };
	SENDS(TL_KEY_KP5, 0, &k_dis_num, "\033[57404;129u");
	SENDS(TL_KEY_KP8, 0, &k_dis, "\033[57419u");
	SENDS(TL_KEY_KPENTER, 0, &k_dis, "\033[57414u");

	/* Round-trip check: every legacy loss is now distinct. */
	struct kitty_decoded d1, d2;
	CHECK(kitty_rt(TL_KEY_H, TL_MOD_CTRL, TL_KEY_PRESS, &k_dis, &d1) &&
		d1.final == 'u' && d1.key == 'h' && d1.mods == TL_MOD_CTRL);
	CHECK(kitty_rt(TL_KEY_BACKSPACE, TL_MOD_CTRL, TL_KEY_PRESS, &k_dis, &d2) &&
		d2.final == 'u' && d2.key == 127 && d2.mods == TL_MOD_CTRL);
	CHECK(d1.key != d2.key);
	CHECK(kitty_rt(TL_KEY_I, TL_MOD_CTRL, TL_KEY_PRESS, &k_dis, &d1) &&
		d1.key == 'i' && d1.mods == TL_MOD_CTRL);
	CHECK(kitty_rt(TL_KEY_M, TL_MOD_CTRL, TL_KEY_PRESS, &k_dis, &d1) &&
		d1.key == 'm' && d1.mods == TL_MOD_CTRL);
	CHECK(kitty_rt(TL_KEY_A, TL_MOD_CTRL | TL_MOD_SHIFT, TL_KEY_PRESS,
		&k_dis, &d1) && d1.key == 'a' &&
		d1.mods == (TL_MOD_CTRL | TL_MOD_SHIFT));
	CHECK(kitty_rt(TL_KEY_LEFTBRACE, TL_MOD_CTRL, TL_KEY_PRESS, &k_dis, &d1) &&
		kitty_rt(TL_KEY_ESC, 0, TL_KEY_PRESS, &k_dis, &d2) &&
		d1.key == '[' && d2.key == 27 && d1.key != d2.key);

	/* Flag 2: event types (press, repeat, release). */
	struct tl_key_modes k_ev = { false, false, false, false,
		TL_KITTY_DISAMBIGUATE | TL_KITTY_EVENTS, NULL };
	SENDS_EV(TL_KEY_A, TL_MOD_CTRL, TL_KEY_REPEAT, &k_ev, "\033[97;5:2u");
	SENDS_EV(TL_KEY_A, TL_MOD_CTRL, TL_KEY_RELEASE, &k_ev, "\033[97;5:3u");
	SENDS_EV(TL_KEY_UP, 0, TL_KEY_RELEASE, &k_ev, "\033[1;1:3A");
	SENDS_EV(TL_KEY_DELETE, 0, TL_KEY_REPEAT, &k_ev, "\033[3;1:2~");
	SENDS_EV(TL_KEY_ESC, 0, TL_KEY_RELEASE, &k_ev, "\033[27;1:3u");
	/* Plain text release without ALL_KEYS sends nothing. */
	SENDS_EV(TL_KEY_A, 0, TL_KEY_RELEASE, &k_ev, "");
	SENDS_EV(TL_KEY_ENTER, 0, TL_KEY_RELEASE, &k_ev, "");

	/* Flag 4: alternate (shifted) keys. */
	struct tl_key_modes k_alt = { false, false, false, false,
		TL_KITTY_DISAMBIGUATE | TL_KITTY_ALTERNATE, NULL };
	SENDS(TL_KEY_A, TL_MOD_CTRL | TL_MOD_SHIFT, &k_alt, "\033[97:65;6u");
	SENDS(TL_KEY_1, TL_MOD_CTRL | TL_MOD_SHIFT, &k_alt, "\033[49:33;6u");
	SENDS(TL_KEY_A, TL_MOD_CTRL, &k_alt, "\033[97;5u");
	SENDS(TL_KEY_SPACE, TL_MOD_CTRL | TL_MOD_SHIFT, &k_alt, "\033[32;6u");

	/* Flag 8: all keys as escape codes (text, Enter/Tab/BS, modifiers). */
	struct tl_key_modes k_all = { false, false, false, false,
		TL_KITTY_ALL_KEYS | TL_KITTY_EVENTS, NULL };
	SENDS_EV(TL_KEY_A, 0, TL_KEY_PRESS, &k_all, "\033[97u");
	SENDS_EV(TL_KEY_A, 0, TL_KEY_RELEASE, &k_all, "\033[97;1:3u");
	SENDS_EV(TL_KEY_A, TL_MOD_SHIFT, TL_KEY_PRESS, &k_all, "\033[97;2u");
	SENDS_EV(TL_KEY_ENTER, 0, TL_KEY_PRESS, &k_all, "\033[13u");
	SENDS_EV(TL_KEY_TAB, 0, TL_KEY_PRESS, &k_all, "\033[9u");
	SENDS_EV(TL_KEY_BACKSPACE, 0, TL_KEY_PRESS, &k_all, "\033[127u");
	SENDS_EV(TL_KEY_LEFTSHIFT, TL_MOD_SHIFT, TL_KEY_PRESS, &k_all,
		"\033[57441;2u");
	SENDS_EV(TL_KEY_LEFTSHIFT, 0, TL_KEY_RELEASE, &k_all, "\033[57441;1:3u");
	SENDS_EV(TL_KEY_LEFTSHIFT, TL_MOD_SHIFT, TL_KEY_REPEAT, &k_all, "");
	SENDS_EV(TL_KEY_CAPSLOCK, 0, TL_KEY_PRESS, &k_all, "\033[57358u");

	/* Flag 16: associated text (with all 5 flags active). */
	struct tl_key_modes k_full = { false, false, false, false,
		TL_KITTY_MASK, NULL };
	struct tl_key_modes k_full_caps = { false, false, false, true,
		TL_KITTY_MASK, NULL };
	struct tl_key_modes k_full_num = { false, false, true, false,
		TL_KITTY_MASK, NULL };
	SENDS_EV(TL_KEY_A, 0, TL_KEY_PRESS, &k_full, "\033[97;;97u");
	SENDS_EV(TL_KEY_A, TL_MOD_SHIFT, TL_KEY_PRESS, &k_full,
		"\033[97:65;2;65u");
	SENDS_EV(TL_KEY_A, 0, TL_KEY_PRESS, &k_full_caps, "\033[97;65;65u");
	SENDS_EV(TL_KEY_A, 0, TL_KEY_REPEAT, &k_full, "\033[97;1:2;97u");
	SENDS_EV(TL_KEY_A, 0, TL_KEY_RELEASE, &k_full, "\033[97;1:3u");
	SENDS_EV(TL_KEY_A, TL_MOD_CTRL, TL_KEY_PRESS, &k_full, "\033[97;5u");
	SENDS_EV(TL_KEY_KP5, 0, TL_KEY_PRESS, &k_full_num, "\033[57404;129;53u");
	CHECK(kitty_rt(TL_KEY_A, TL_MOD_SHIFT, TL_KEY_PRESS, &k_full, &d1) &&
		d1.key == 'a' && d1.shifted == 'A' && d1.mods == TL_MOD_SHIFT &&
		d1.ev == TL_KEY_PRESS && d1.text == 'A');

	/* Key trace (--keys, 0.1.1): evdev names, layout char, formatting,
	 * truncation safety, and byte-for-byte match with pty output. */
	CHECK(strcmp(tl_evdev_name(TL_KEY_ESC), "KEY_ESC") == 0);
	CHECK(strcmp(tl_evdev_name(TL_KEY_C), "KEY_C") == 0);
	CHECK(strcmp(tl_evdev_name(TL_KEY_LEFTCTRL), "KEY_LEFTCTRL") == 0);
	CHECK(strcmp(tl_evdev_name(TL_KEY_COMPOSE), "KEY_COMPOSE") == 0);
	CHECK(tl_evdev_name(0) == NULL);
	CHECK(tl_evdev_name(TL_KEY_COUNT) == NULL);
	CHECK(tl_evdev_name(9999) == NULL);

	CHECK(tl_key_char(TL_KEY_A, 0, &plain) == 'a');
	CHECK(tl_key_char(TL_KEY_A, TL_MOD_SHIFT, &plain) == 'A');
	CHECK(tl_key_char(TL_KEY_KP5, 0, &num) == '5');
	CHECK(tl_key_char(TL_KEY_KP5, 0, &plain) == 0);
	CHECK(tl_key_char(TL_KEY_UP, 0, &plain) == 0);

	{
		char line[256];
		char seq[TL_KEY_SEQ_MAX];
		size_t n;

		n = tl_key_encode_event(TL_KEY_C, TL_MOD_CTRL, TL_KEY_PRESS,
			&plain, seq);
		tl_key_trace_format(line, sizeof(line), TL_KEY_C, TL_MOD_CTRL,
			TL_KEY_PRESS, &plain, seq, n, NULL);
		CHECK(strcmp(line,
			"46 KEY_C press mods=Ctrl locks=- char='c' pty=\"\\x03\" [03]") == 0);

		n = tl_key_encode_event(TL_KEY_H, TL_MOD_CTRL, TL_KEY_PRESS,
			&k_dis, seq);
		tl_key_trace_format(line, sizeof(line), TL_KEY_H, TL_MOD_CTRL,
			TL_KEY_PRESS, &k_dis, seq, n, NULL);
		CHECK(strcmp(line,
			"35 KEY_H press mods=Ctrl locks=- char='h' kitty=1 "
			"pty=\"ESC[104;5u\" [1b 5b 31 30 34 3b 35 75]") == 0);

		n = tl_key_encode_event(TL_KEY_LEFTCTRL, TL_MOD_CTRL,
			TL_KEY_PRESS, &plain, seq);
		tl_key_trace_format(line, sizeof(line), TL_KEY_LEFTCTRL,
			TL_MOD_CTRL, TL_KEY_PRESS, &plain, seq, n, NULL);
		CHECK(strcmp(line,
			"29 KEY_LEFTCTRL press mods=Ctrl locks=- char=- pty=(none)") == 0);

		tl_key_trace_format(line, sizeof(line), TL_KEY_C,
			TL_MOD_CTRL | TL_MOD_SHIFT, TL_KEY_PRESS, &plain,
			NULL, 0, "copy");
		CHECK(strcmp(line,
			"46 KEY_C press mods=Shift+Ctrl locks=- char='C' "
			"pty=(none) [copy]") == 0);

		struct tl_key_modes both_locks = { false, false, true, true,
			TL_KITTY_DISAMBIGUATE | TL_KITTY_EVENTS, NULL };
		n = tl_key_encode_event(TL_KEY_A, TL_MOD_ALT, TL_KEY_RELEASE,
			&both_locks, seq);
		tl_key_trace_format(line, sizeof(line), TL_KEY_A, TL_MOD_ALT,
			TL_KEY_RELEASE, &both_locks, seq, n, NULL);
		CHECK(strstr(line, "30 KEY_A release mods=Alt locks=Caps+Num char='A' kitty=3") != NULL);

		/* Unknown evdev code and small-buffer truncation safety. */
		tl_key_trace_format(line, sizeof(line), 200, 0, TL_KEY_PRESS,
			&plain, NULL, 0, NULL);
		CHECK(strcmp(line,
			"200 UNKNOWN press mods=- locks=- char=- pty=(none)") == 0);
		char tiny[12];
		size_t wlen = tl_key_trace_format(tiny, sizeof(tiny), TL_KEY_A,
			0, TL_KEY_PRESS, &plain, "a", 1, NULL);
		CHECK(wlen == 11 && tiny[11] == '\0');
		CHECK(tl_key_trace_format(tiny, 0, TL_KEY_A, 0, TL_KEY_PRESS,
			&plain, "a", 1, NULL) == 0);

		/* Acceptance: hex bytes in --keys trace match exact pty bytes. */
		const struct {
			uint32_t code;
			unsigned mods;
			enum tl_key_event ev;
			const struct tl_key_modes *m;
		} cases[] = {
			{ TL_KEY_A, 0, TL_KEY_PRESS, &plain },
			{ TL_KEY_C, TL_MOD_CTRL, TL_KEY_PRESS, &plain },
			{ TL_KEY_UP, TL_MOD_CTRL, TL_KEY_PRESS, &plain },
			{ TL_KEY_BACKSPACE, TL_MOD_ALT, TL_KEY_PRESS, &plain },
			{ TL_KEY_H, TL_MOD_CTRL, TL_KEY_PRESS, &k_dis },
			{ TL_KEY_A, TL_MOD_SHIFT, TL_KEY_PRESS, &k_full },
			{ TL_KEY_A, TL_MOD_CTRL, TL_KEY_REPEAT, &k_ev },
			{ TL_KEY_A, TL_MOD_CTRL, TL_KEY_RELEASE, &k_ev },
			{ TL_KEY_KP5, 0, TL_KEY_PRESS, &k_full_num },
		};
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
			n = tl_key_encode_event(cases[i].code, cases[i].mods,
				cases[i].ev, cases[i].m, seq);
			tl_key_trace_format(line, sizeof(line), cases[i].code,
				cases[i].mods, cases[i].ev, cases[i].m,
				seq, n, NULL);
			const char *lb = strrchr(line, '[');
			const char *rb = strrchr(line, ']');
			CHECK(lb != NULL && rb != NULL && rb > lb);
			unsigned char parsed[TL_KEY_SEQ_MAX];
			size_t pn = 0;
			const char *p = lb + 1;
			while (p < rb && pn < sizeof(parsed)) {
				unsigned val = 0;
				int consumed = 0;
				if (sscanf(p, "%2x%n", &val, &consumed) != 1 ||
				    consumed != 2)
					break;
				parsed[pn++] = (unsigned char)val;
				p += consumed;
				if (p < rb && *p == ' ')
					p++;
			}
			CHECK(pn == n && memcmp(parsed, seq, n) == 0);
		}
	}

	/* Compiled-in layouts, AltGr, UTF-8, dead keys, and Compose (0.1.2). */
	{
		CHECK(tl_layout_count() == 7);
		const struct tl_layout *l_us = tl_layout_find("us");
		const struct tl_layout *l_intl = tl_layout_find("us-intl");
		const struct tl_layout *l_dvorak = tl_layout_find("dvorak");
		const struct tl_layout *l_colemak = tl_layout_find("colemak");
		const struct tl_layout *l_uk = tl_layout_find("uk");
		const struct tl_layout *l_de = tl_layout_find("de");
		const struct tl_layout *l_fr = tl_layout_find("fr");
		CHECK(l_us != NULL && l_us == tl_layout_default());
		CHECK(l_intl != NULL && l_dvorak != NULL && l_colemak != NULL);
		CHECK(l_uk != NULL && l_de != NULL && l_fr != NULL);
		CHECK(tl_layout_find("nonexistent") == NULL);
		CHECK(tl_layout_find("") == NULL);
		CHECK(tl_layout_find(NULL) == NULL);

		/* RightAlt is Alt on us/dvorak, AltGr on us-intl/colemak/uk/de/fr. */
		struct tl_kbd ks;
		tl_kbd_init(&ks);
		tl_kbd_key(&ks, TL_KEY_RIGHTALT, true);
		CHECK(tl_kbd_mods(&ks) == TL_MOD_ALT);
		tl_kbd_set_layout(&ks, l_intl);
		CHECK(tl_kbd_mods(&ks) == TL_MOD_ALTGR);
		tl_kbd_key(&ks, TL_KEY_LEFTALT, true);
		CHECK(tl_kbd_mods(&ks) == (TL_MOD_ALT | TL_MOD_ALTGR));
		tl_kbd_set_layout(&ks, l_dvorak);
		CHECK(tl_kbd_mods(&ks) == TL_MOD_ALT);
		tl_kbd_leave(&ks);

		/* Layout lookups across all 7 layouts (base, Shift, AltGr, Caps). */
		/* us-intl */
		CHECK(tl_layout_lookup(l_intl, TL_KEY_E, TL_MOD_ALTGR, false) == 0x00e9u); /* é */
		CHECK(tl_layout_lookup(l_intl, TL_KEY_E, TL_MOD_ALTGR | TL_MOD_SHIFT, false) == 0x00c9u); /* É */
		CHECK(tl_layout_lookup(l_intl, TL_KEY_E, TL_MOD_ALTGR, true) == 0x00c9u); /* CapsLock + AltGr */
		CHECK(tl_layout_lookup(l_intl, TL_KEY_5, TL_MOD_ALTGR, false) == 0x20acu); /* € */
		CHECK(tl_layout_lookup(l_intl, TL_KEY_APOSTROPHE, 0, false) ==
			(TL_KEY_DEAD_BIT | TL_DEAD_ACUTE));
		CHECK(tl_layout_lookup(l_intl, TL_KEY_APOSTROPHE, TL_MOD_SHIFT, false) ==
			(TL_KEY_DEAD_BIT | TL_DEAD_DIAERESIS));
		CHECK(strcmp(tl_dead_key_name(TL_KEY_DEAD_BIT | TL_DEAD_ACUTE),
			"dead_acute") == 0);
		CHECK(tl_dead_key_name('a') == NULL);

		/* dvorak */
		CHECK(tl_layout_lookup(l_dvorak, TL_KEY_S, 0, false) == 'o');
		CHECK(tl_layout_lookup(l_dvorak, TL_KEY_D, TL_MOD_SHIFT, false) == 'E');
		CHECK(tl_layout_lookup(l_dvorak, TL_KEY_Q, 0, false) == '\'');

		/* colemak */
		CHECK(tl_layout_lookup(l_colemak, TL_KEY_E, 0, false) == 'f');
		CHECK(tl_layout_lookup(l_colemak, TL_KEY_K, 0, false) == 'e');
		CHECK(tl_layout_lookup(l_colemak, TL_KEY_K, TL_MOD_ALTGR, false) == 0x00e9u); /* é */
		CHECK(tl_layout_lookup(l_colemak, TL_KEY_MINUS, TL_MOD_ALTGR | TL_MOD_SHIFT, false) == 0x2014u); /* — */

		/* uk */
		CHECK(tl_layout_lookup(l_uk, TL_KEY_3, TL_MOD_SHIFT, false) == 0x00a3u); /* £ */
		CHECK(tl_layout_lookup(l_uk, TL_KEY_2, TL_MOD_SHIFT, false) == '"');
		CHECK(tl_layout_lookup(l_uk, TL_KEY_APOSTROPHE, TL_MOD_SHIFT, false) == '@');
		CHECK(tl_layout_lookup(l_uk, TL_KEY_102ND, 0, false) == '\\');

		/* de */
		CHECK(tl_layout_lookup(l_de, TL_KEY_Y, 0, false) == 'z');
		CHECK(tl_layout_lookup(l_de, TL_KEY_Z, 0, false) == 'y');
		CHECK(tl_layout_lookup(l_de, TL_KEY_MINUS, 0, false) == 0x00dfu); /* ß */
		CHECK(tl_layout_lookup(l_de, TL_KEY_APOSTROPHE, 0, false) == 0x00e4u); /* ä */
		CHECK(tl_layout_lookup(l_de, TL_KEY_APOSTROPHE, 0, true) == 0x00c4u); /* Ä via Caps */
		CHECK(tl_layout_lookup(l_de, TL_KEY_Q, TL_MOD_ALTGR, false) == '@');
		CHECK(tl_layout_lookup(l_de, TL_KEY_102ND, TL_MOD_ALTGR, false) == '|');

		/* fr */
		CHECK(tl_layout_lookup(l_fr, TL_KEY_Q, 0, false) == 'a');
		CHECK(tl_layout_lookup(l_fr, TL_KEY_W, 0, false) == 'z');
		CHECK(tl_layout_lookup(l_fr, TL_KEY_2, 0, false) == 0x00e9u); /* é */
		CHECK(tl_layout_lookup(l_fr, TL_KEY_2, 0, true) == 0x00e9u); /* Caps doesn't turn é into 2 */
		CHECK(tl_layout_lookup(l_fr, TL_KEY_E, TL_MOD_ALTGR, false) == 0x20acu); /* € */

		/* UTF-8 encoding in legacy and Kitty modes. */
		struct tl_key_modes m_intl = { false, false, false, false, 0, l_intl };
		struct tl_key_modes m_intl_kdis = { false, false, false, false,
			TL_KITTY_DISAMBIGUATE, l_intl };
		struct tl_key_modes m_intl_kfull = { false, false, false, false,
			TL_KITTY_MASK, l_intl };
		SENDS(TL_KEY_E, TL_MOD_ALTGR, &m_intl, "\xc3\xa9");          /* é */
		SENDS(TL_KEY_5, TL_MOD_ALTGR, &m_intl, "\xe2\x82\xac");      /* € */
		SENDS(TL_KEY_E, TL_MOD_ALTGR | TL_MOD_ALT, &m_intl, "\033\xc3\xa9");
		SENDS(TL_KEY_APOSTROPHE, 0, &m_intl, "");                     /* dead key emits nothing directly */
		SENDS(TL_KEY_E, TL_MOD_ALTGR, &m_intl_kdis, "\xc3\xa9");      /* plain UTF-8 under disambig */
		SENDS_EV(TL_KEY_E, TL_MOD_ALTGR, TL_KEY_PRESS, &m_intl_kfull,
			"\033[101;;233u");                                    /* CSI u with é text */

		/* Dead-key state machine. */
		struct tl_compose comp;
		uint32_t out_cp = 0;
		tl_compose_init(&comp);
		CHECK(!tl_compose_active(&comp));
		CHECK(tl_compose_feed(&comp, 'a', &out_cp) == TL_COMPOSE_IDLE);
		CHECK(tl_compose_feed(&comp, TL_KEY_DEAD_BIT | TL_DEAD_ACUTE, &out_cp) ==
			TL_COMPOSE_PENDING);
		CHECK(tl_compose_active(&comp));
		CHECK(tl_compose_feed(&comp, 'e', &out_cp) == TL_COMPOSE_DONE &&
			out_cp == 0x00e9u); /* é */
		CHECK(!tl_compose_active(&comp));
		/* Dead key + space -> fallback ASCII; dead + dead -> spacing accent. */
		CHECK(tl_compose_feed(&comp, TL_KEY_DEAD_BIT | TL_DEAD_ACUTE, &out_cp) ==
			TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, ' ', &out_cp) == TL_COMPOSE_DONE &&
			out_cp == '\'');
		CHECK(tl_compose_feed(&comp, TL_KEY_DEAD_BIT | TL_DEAD_ACUTE, &out_cp) ==
			TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, TL_KEY_DEAD_BIT | TL_DEAD_ACUTE, &out_cp) ==
			TL_COMPOSE_DONE && out_cp == 0x00b4u); /* ´ */
		/* Dead tilde + n -> ñ; dead diaeresis + U -> Ü; mismatch cancels. */
		CHECK(tl_compose_feed(&comp, TL_KEY_DEAD_BIT | TL_DEAD_TILDE, &out_cp) ==
			TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, 'n', &out_cp) == TL_COMPOSE_DONE &&
			out_cp == 0x00f1u); /* ñ */
		CHECK(tl_compose_feed(&comp, TL_KEY_DEAD_BIT | TL_DEAD_DIAERESIS, &out_cp) ==
			TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, 'U', &out_cp) == TL_COMPOSE_DONE &&
			out_cp == 0x00dcu); /* Ü */
		CHECK(tl_compose_feed(&comp, TL_KEY_DEAD_BIT | TL_DEAD_ACUTE, &out_cp) ==
			TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, 'x', &out_cp) == TL_COMPOSE_CANCELLED);
		CHECK(!tl_compose_active(&comp));

		/* Multi-key Compose sequences (2-key and 3-key). */
		CHECK(tl_compose_feed(&comp, TL_COMPOSE_START, &out_cp) == TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, 'c', &out_cp) == TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, '=', &out_cp) == TL_COMPOSE_DONE &&
			out_cp == 0x20acu); /* € */
		CHECK(tl_compose_feed(&comp, TL_COMPOSE_START, &out_cp) == TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, '-', &out_cp) == TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, '-', &out_cp) == TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, '-', &out_cp) == TL_COMPOSE_DONE &&
			out_cp == 0x2014u); /* — */
		CHECK(tl_compose_feed(&comp, TL_COMPOSE_START, &out_cp) == TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, '(', &out_cp) == TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, 'c', &out_cp) == TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, ')', &out_cp) == TL_COMPOSE_DONE &&
			out_cp == 0x00a9u); /* © */
		/* Compose mismatch cancels cleanly. */
		CHECK(tl_compose_feed(&comp, TL_COMPOSE_START, &out_cp) == TL_COMPOSE_PENDING);
		CHECK(tl_compose_feed(&comp, 'q', &out_cp) == TL_COMPOSE_CANCELLED);
		CHECK(!tl_compose_active(&comp));

		/* tl_key_encode_text encodes composed codepoints as UTF-8. */
		char seq[TL_KEY_SEQ_MAX];
		CHECK(tl_key_encode_text(0x2014u, seq) == 3 &&
			memcmp(seq, "\xe2\x80\x94", 3) == 0);

		/* --keys trace shows AltGr and dead keys cleanly. */
		char line[256];
		size_t n = tl_key_encode_event(TL_KEY_E, TL_MOD_ALTGR,
			TL_KEY_PRESS, &m_intl, seq);
		tl_key_trace_format(line, sizeof(line), TL_KEY_E, TL_MOD_ALTGR,
			TL_KEY_PRESS, &m_intl, seq, n, NULL);
		CHECK(strstr(line, "mods=AltGr") != NULL);
		CHECK(strstr(line, "char=U+00E9") != NULL);
		CHECK(strstr(line, "[c3 a9]") != NULL);

		tl_key_trace_format(line, sizeof(line), TL_KEY_APOSTROPHE, 0,
			TL_KEY_PRESS, &m_intl, NULL, 0, "dead_acute");
		CHECK(strstr(line, "char=dead_acute") != NULL);
		CHECK(strstr(line, "[dead_acute]") != NULL);

		/* --keys IME trace formatting (text-input-v3). */
		tl_key_trace_ime(line, sizeof(line), "commit", "\xe3\x81\x82", 3, 0, 0);
		CHECK(strstr(line, "ime commit pty=") != NULL);
		CHECK(strstr(line, "[e3 81 82]") != NULL);
		tl_key_trace_ime(line, sizeof(line), "preedit", "nihao", 5, 0, 5);
		CHECK(strcmp(line, "ime preedit cursor=0..5 text=\"nihao\"") == 0);
		tl_key_trace_ime(line, sizeof(line), "preedit", NULL, 0, 0, 0);
		CHECK(strcmp(line, "ime preedit (clear)") == 0);
	}

	CHECK_DONE("test_keys");
}
