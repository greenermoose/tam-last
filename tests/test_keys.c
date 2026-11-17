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

static const struct tl_key_modes plain = { false, false, false, false };

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
	struct tl_key_modes app = { true, false, false, false };
	struct tl_key_modes num = { false, false, true, false };
	struct tl_key_modes caps = { false, false, false, true };
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

	CHECK_DONE("test_keys");
}
