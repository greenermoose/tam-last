/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Bytes in, cells out: the parser and the screen together. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "tam-last/osc.h"
#include "tam-last/select.h"
#include "tam-last/term.h"

static struct tl_term t;

static void feed(const char *s)
{
	tl_term_feed(&t, s, strlen(s));
}

/* Row y as text, trailing blanks removed; wide tails skipped. */
static const char *row(int y)
{
	static char buf[1024];
	size_t n = 0;
	for (int x = 0; x < t.cols && n < sizeof(buf) - 5; x++) {
		uint32_t cp = tl_term_cell(&t, x, y)->cp;
		if (cp == TL_CELL_WIDE_TAIL)
			continue;
		if (cp < 0x80) {
			buf[n++] = (char)cp;
		} else {
			buf[n++] = '?';
		}
	}
	while (n > 0 && buf[n - 1] == ' ')
		n--;
	buf[n] = '\0';
	return buf;
}

static void fresh(int cols, int rows)
{
	tl_term_free(&t);
	CHECK(tl_term_init(&t, cols, rows) == 0);
}

int main(void)
{
	setlocale(LC_CTYPE, "C.UTF-8");
	CHECK(tl_term_init(&t, 20, 5) == 0);

	feed("hello");
	CHECK(strcmp(row(0), "hello") == 0);
	CHECK(t.cx == 5 && t.cy == 0);

	feed("\r\nworld");
	CHECK(strcmp(row(1), "world") == 0);
	feed("\b\bX");
	CHECK(strcmp(row(1), "worXd") == 0);
	feed("\tT");
	CHECK(tl_term_cell(&t, 8, 1)->cp == 'T');

	/* Cursor addressing, erasing. */
	feed("\033[3;5HX");
	CHECK(tl_term_cell(&t, 4, 2)->cp == 'X');
	feed("\033[H\033[2J");
	CHECK(strcmp(row(0), "") == 0 && strcmp(row(2), "") == 0);
	feed("abcdef\033[3D\033[K");
	CHECK(strcmp(row(0), "abc") == 0);
	feed("\033[1K");
	CHECK(strcmp(row(0), "") == 0);
	feed("\033[10;10H");
	CHECK(t.cx == 9 && t.cy == 4); /* clamped to the screen */
	feed("\033[99999999999A");
	CHECK(t.cy == 0);

	/* Insert, delete, erase characters. */
	feed("\033[Habcdef\r\033[2P");
	CHECK(strcmp(row(0), "cdef") == 0);
	feed("\r\033[2@");
	CHECK(strcmp(row(0), "  cdef") == 0);
	feed("\r\033[3X");
	CHECK(strcmp(row(0), "   def") == 0);

	/* Colors and attributes. */
	fresh(20, 5);
	feed("\033[31mR\033[38;5;200mI\033[38;2;1;2;3mG\033[38:2::4:5:6mB");
	CHECK(tl_term_cell(&t, 0, 0)->fg == TL_COLOR_INDEX(1));
	CHECK(tl_term_cell(&t, 1, 0)->fg == TL_COLOR_INDEX(200));
	CHECK(tl_term_cell(&t, 2, 0)->fg == TL_COLOR_RGB(0x010203));
	CHECK(tl_term_cell(&t, 3, 0)->fg == TL_COLOR_RGB(0x040506));
	feed("\033[1;4;7;44mx\033[0my");
	CHECK(tl_term_cell(&t, 4, 0)->attrs ==
		(TL_ATTR_BOLD | TL_ATTR_UNDERLINE | TL_ATTR_INVERSE));
	CHECK(tl_term_cell(&t, 4, 0)->bg == TL_COLOR_INDEX(4));
	CHECK(tl_term_cell(&t, 5, 0)->attrs == 0 &&
		tl_term_cell(&t, 5, 0)->fg == TL_COLOR_DEFAULT);
	feed("\033[92;105mz");
	CHECK(tl_term_cell(&t, 6, 0)->fg == TL_COLOR_INDEX(10));
	CHECK(tl_term_cell(&t, 6, 0)->bg == TL_COLOR_INDEX(13));
	/* Erase uses the current background. */
	feed("\033[42m\033[K");
	CHECK(tl_term_cell(&t, 10, 0)->bg == TL_COLOR_INDEX(2));
	feed("\033[m");

	/* Wrapping: the last column waits for the next character. */
	fresh(10, 3);
	feed("0123456789");
	CHECK(t.cy == 0 && t.cx == 9 && t.wrap_next);
	feed("\r");
	CHECK(t.cy == 0 && t.cx == 0);
	feed("\033[10Gab");
	CHECK(strcmp(row(1), "b") == 0);
	feed("\033[?7l\033[1;9Hxyz");
	CHECK(strcmp(row(0), "01234567xz") == 0);
	feed("\033[?7h");

	/* Scrolling at the bottom. */
	fresh(10, 3);
	feed("one\r\ntwo\r\nthree\r\nfour");
	CHECK(strcmp(row(0), "two") == 0 && strcmp(row(2), "four") == 0);
	feed("\033[H\033M");
	CHECK(strcmp(row(0), "") == 0 && strcmp(row(1), "two") == 0);
	feed("\033[2;1H\033[L");
	CHECK(strcmp(row(1), "") == 0 && strcmp(row(2), "two") == 0);
	feed("\033[M");
	CHECK(strcmp(row(1), "two") == 0);

	/* UTF-8, split across reads, broken, and wide. */
	fresh(10, 3);
	tl_term_feed(&t, "\xc3", 1);
	tl_term_feed(&t, "\xa9", 1);
	CHECK(tl_term_cell(&t, 0, 0)->cp == 0xE9);
	feed("\xff");
	CHECK(tl_term_cell(&t, 1, 0)->cp == 0xFFFD);
	feed("\xe2\x82X");
	CHECK(tl_term_cell(&t, 2, 0)->cp == 0xFFFD &&
		tl_term_cell(&t, 3, 0)->cp == 'X');
	feed("\xe6\xbc\xa2"); /* a wide CJK character */
	CHECK(tl_term_cell(&t, 4, 0)->cp == 0x6F22);
	CHECK(tl_term_cell(&t, 5, 0)->cp == TL_CELL_WIDE_TAIL);
	CHECK(t.cx == 6);
	feed("\033[1;6Hq"); /* overwrite the tail: the head goes too */
	CHECK(tl_term_cell(&t, 4, 0)->cp == ' ');
	feed("\033[1;10H\xe6\xbc\xa2"); /* no room: wraps first */
	CHECK(tl_term_cell(&t, 0, 1)->cp == 0x6F22);

	/* Strings are skipped, with either terminator. */
	fresh(20, 3);
	feed("\033]0;a title\007ok\033]2;more\033\\!\033P1$r\033\\.");
	CHECK(strcmp(row(0), "ok!.") == 0);

	/* Queries get answers. */
	feed("\033[2;3H\033[6n\033[5n\033[c");
	CHECK(t.reply_len > 0);
	CHECK(memcmp(t.reply, "\033[2;3R\033[0n\033[?62;22c", t.reply_len) == 0);
	t.reply_len = 0;

	/* Modes. */
	feed("\033[?1h\033=\033[?25l");
	CHECK(t.app_cursor && t.app_keypad && !t.cursor_visible);
	feed("\033[?1l\033>\033[?25h");
	CHECK(!t.app_cursor && !t.app_keypad && t.cursor_visible);

	/* Save and restore. */
	feed("\033[3;4H\033[33m\0337\033[H\033[m\0338Z");
	CHECK(tl_term_cell(&t, 3, 2)->cp == 'Z' &&
		tl_term_cell(&t, 3, 2)->fg == TL_COLOR_INDEX(3));

	/* Resizing keeps the cursor's row on screen. */
	fresh(10, 10);
	feed("\033[8;1Hkeep");
	CHECK(tl_term_resize(&t, 6, 4) == 0);
	CHECK(t.cy == 3 && strcmp(row(3), "keep") == 0);
	CHECK(tl_term_resize(&t, 30, 12) == 0);
	CHECK(strcmp(row(3), "keep") == 0 && t.cols == 30);

	/* Scroll regions (DECSTBM), origin mode (DECOM), insert mode (IRM). */
	fresh(10, 5);
	feed("r1\r\nr2\r\nr3\r\nr4\r\nr5");
	feed("\033[2;4r"); /* region: rows 2..4 (1-based) -> 1..3 (0-based) */
	CHECK(t.cx == 0 && t.cy == 0 && t.scroll_top == 1 && t.scroll_bottom == 3);
	feed("\033[4;1H\n"); /* at bottom of region: scrolls 1..3 */
	CHECK(strcmp(row(0), "r1") == 0 && strcmp(row(1), "r3") == 0 &&
		strcmp(row(2), "r4") == 0 && strcmp(row(3), "") == 0 &&
		strcmp(row(4), "r5") == 0);
	feed("\033[2;1H\033M"); /* RI at top of region: scrolls down 1..3 */
	CHECK(strcmp(row(1), "") == 0 && strcmp(row(2), "r3") == 0 &&
		strcmp(row(3), "r4") == 0);
	feed("\033[2;1H\033[S\033[T"); /* SU and SD stay inside region */
	CHECK(strcmp(row(0), "r1") == 0 && strcmp(row(4), "r5") == 0);
	feed("\033[3;1H\033[L"); /* IL inside region */
	CHECK(strcmp(row(3), "r3") == 0 && strcmp(row(4), "r5") == 0);
	feed("\033[1;1H\033[L"); /* IL outside region: no-op */
	CHECK(strcmp(row(0), "r1") == 0);
	feed("\033[?6h"); /* DECOM on: homes to scroll_top */
	CHECK(t.origin_mode && t.cy == 1);
	feed("\033[1;1HX\033[99;1HY"); /* clamped to [scroll_top, scroll_bottom] */
	CHECK(tl_term_cell(&t, 0, 1)->cp == 'X' &&
		tl_term_cell(&t, 0, 3)->cp == 'Y');
	feed("\033[?6l");
	CHECK(!t.origin_mode && t.cy == 0);
	feed("\033[1;1Habc\033[1;2H\033[4hZ\033[4l"); /* IRM */
	CHECK(tl_term_cell(&t, 0, 0)->cp == 'a' &&
		tl_term_cell(&t, 1, 0)->cp == 'Z' &&
		tl_term_cell(&t, 2, 0)->cp == 'b' &&
		tl_term_cell(&t, 3, 0)->cp == 'c');

	/* Alternate screen (47, 1047, 1049) and resize of both grids. */
	fresh(10, 4);
	feed("main\033[2;3H");
	feed("\033[?1049h");
	CHECK(t.alt_screen && strcmp(row(0), "") == 0);
	feed("\033[1;1Halt");
	CHECK(tl_term_resize(&t, 12, 5) == 0);
	CHECK(strcmp(row(0), "alt") == 0);
	feed("\033[?1049l");
	CHECK(!t.alt_screen && strcmp(row(0), "main") == 0 &&
		t.cx == 2 && t.cy == 1);
	feed("\033[?47h");
	CHECK(t.alt_screen && strcmp(row(0), "alt") == 0);
	feed("\033[?1047l");
	CHECK(!t.alt_screen && strcmp(row(0), "main") == 0);

	/* DEC special graphics (ESC ( 0 / ESC ( B). */
	fresh(10, 2);
	feed("\033(0lqk\033(Babc");
	CHECK(tl_term_cell(&t, 0, 0)->cp == 0x250c &&
		tl_term_cell(&t, 1, 0)->cp == 0x2500 &&
		tl_term_cell(&t, 2, 0)->cp == 0x2510 &&
		tl_term_cell(&t, 3, 0)->cp == 'a');

	/* Combining characters stored with their base cell. */
	fresh(10, 2);
	feed("e\xcc\x81\xcc\x82\xcc\x83"); /* e + 3 combining marks; cap is 2 */
	CHECK(tl_term_cell(&t, 0, 0)->cp == 'e' &&
		tl_term_cell(&t, 0, 0)->ncomb == 2 &&
		tl_term_cell(&t, 0, 0)->comb[0] == 0x0301 &&
		tl_term_cell(&t, 0, 0)->comb[1] == 0x0302);

	/* Cursor styles (DECSCUSR). */
	feed("\033[4 q");
	CHECK(t.cursor_style == TL_CURSOR_UNDERLINE);
	feed("\033[5 q");
	CHECK(t.cursor_style == TL_CURSOR_BAR);
	feed("\033[0 q");
	CHECK(t.cursor_style == TL_CURSOR_BLOCK);

	/* Row dirty tracking. */
	memset(t.dirty_rows, 0, (size_t)t.rows * sizeof(bool));
	t.full_dirty = false;
	t.dirty = false;
	feed("\033[2;1Hx");
	CHECK(t.dirty && !t.full_dirty && !t.dirty_rows[0] && t.dirty_rows[1]);

	/* Mouse reporting and bracketed paste modes (1000, 1002, 1003, 1006, 2004). */
	feed("\033[?1000h");
	CHECK(t.mouse_mode == TL_MOUSE_X10);
	feed("\033[?1002h\033[?1006h\033[?2004h");
	CHECK(t.mouse_mode == TL_MOUSE_DRAG && t.mouse_sgr && t.bracketed_paste);
	feed("\033[?1003h");
	CHECK(t.mouse_mode == TL_MOUSE_ANY);
	feed("\033[?1003l\033[?1006l\033[?2004l");
	CHECK(t.mouse_mode == TL_MOUSE_NONE && !t.mouse_sgr && !t.bracketed_paste);

	/* Scrollback pages, view_offset, ED 3, and selection extraction. */
	tl_term_free(&t);
	CHECK(tl_term_init(&t, 12, 3) == 0);
	feed("alpha beta\r\ngamma\r\ndelta\r\nepsilon");
	CHECK(t.sb_len == 1);
	int ncols = 0;
	const struct tl_cell *r0 = tl_term_abs_row(&t, 0, &ncols);
	CHECK(r0 != NULL && ncols == 12 && r0[0].cp == 'a');
	tl_term_scroll_view(&t, 1);
	CHECK(t.view_offset == 1);
	CHECK(tl_term_view_row(&t, 0, &ncols)[0].cp == 'a');
	tl_term_reset_view(&t);
	CHECK(t.view_offset == 0);

	/* Word, line, and char selection extraction. */
	struct tl_sel sel;
	tl_sel_start(&sel, &t, 7, 0, TL_SEL_WORD); /* "beta" on abs_y 0 */
	tl_sel_end(&sel);
	size_t slen = 0;
	char *stext = tl_sel_extract(&t, &sel, &slen);
	CHECK(stext != NULL && strcmp(stext, "beta") == 0 && slen == 4);
	free(stext);

	tl_sel_start(&sel, &t, 2, 1, TL_SEL_LINE);
	tl_sel_end(&sel);
	stext = tl_sel_extract(&t, &sel, &slen);
	CHECK(stext != NULL && strcmp(stext, "gamma") == 0);
	free(stext);

	/* ED 3 clears scrollback. */
	feed("\033[3J");
	CHECK(t.sb_len == 0 && t.view_offset == 0);

	/* Scrollback cap eviction (TL_SB_MAX_ROWS). */
	for (int i = 0; i < TL_SB_MAX_ROWS + 200; i++)
		feed("\r\nx");
	CHECK(t.sb_len <= TL_SB_MAX_ROWS + TL_SB_PAGE_ROWS);

	/* Kitty keyboard protocol: query, set/OR/clear, push/pop, per-screen stack, RIS. */
	fresh(20, 4);
	t.reply_len = 0;
	feed("\033[?u");
	CHECK(t.reply_len == 5 && memcmp(t.reply, "\033[?0u", 5) == 0);
	CHECK(tl_term_kitty_kbd_flags(&t) == 0);

	/* CSI = flags ; mode u (default mode 1 = replace, 2 = OR, 3 = clear). */
	feed("\033[=1u");
	CHECK(tl_term_kitty_kbd_flags(&t) == 1);
	feed("\033[=4;2u");
	CHECK(tl_term_kitty_kbd_flags(&t) == 5);
	feed("\033[=1;3u");
	CHECK(tl_term_kitty_kbd_flags(&t) == 4);
	feed("\033[=255;1u"); /* masks to 5 valid bits (0x1f = 31) */
	CHECK(tl_term_kitty_kbd_flags(&t) == 31);
	t.reply_len = 0;
	feed("\033[?u");
	CHECK(t.reply_len == 6 && memcmp(t.reply, "\033[?31u", 6) == 0);

	/* Push and pop on primary screen. */
	feed("\033[=0u");
	feed("\033[>1u\033[>3u\033[>7u");
	CHECK(tl_term_kitty_kbd_flags(&t) == 7);
	feed("\033[<u"); /* default pop = 1 */
	CHECK(tl_term_kitty_kbd_flags(&t) == 3);
	feed("\033[<2u");
	CHECK(tl_term_kitty_kbd_flags(&t) == 0);
	feed("\033[<99u"); /* over-pop clamps cleanly to 0 */
	CHECK(tl_term_kitty_kbd_flags(&t) == 0);

	/* Bounded stack eviction: pushing more than TL_KITTY_KBD_STACK_MAX keeps newest. */
	for (int i = 1; i <= TL_KITTY_KBD_STACK_MAX + 5; i++) {
		char seq[16];
		snprintf(seq, sizeof(seq), "\033[>%du", i & 0x1f);
		feed(seq);
	}
	CHECK(tl_term_kitty_kbd_flags(&t) == ((TL_KITTY_KBD_STACK_MAX + 5) & 0x1f));
	feed("\033[<1u");
	CHECK(tl_term_kitty_kbd_flags(&t) == ((TL_KITTY_KBD_STACK_MAX + 4) & 0x1f));

	/* Independent primary vs alternate screen stacks. */
	feed("\033[<99u\033[>5u");
	CHECK(tl_term_kitty_kbd_flags(&t) == 5);
	feed("\033[?1049h");
	CHECK(t.alt_screen && tl_term_kitty_kbd_flags(&t) == 0);
	feed("\033[>11u");
	CHECK(tl_term_kitty_kbd_flags(&t) == 11);
	t.reply_len = 0;
	feed("\033[?u");
	CHECK(t.reply_len == 6 && memcmp(t.reply, "\033[?11u", 6) == 0);
	feed("\033[?1049l");
	CHECK(!t.alt_screen && tl_term_kitty_kbd_flags(&t) == 5);

	/* RIS (ESC c) resets both screens' kitty stacks. */
	feed("\033c");
	CHECK(tl_term_kitty_kbd_flags(&t) == 0);
	feed("\033[?1049h");
	CHECK(tl_term_kitty_kbd_flags(&t) == 0);
	feed("\033[?1049l");

	/* --- OSC 0 & 2: window titles --- */
	fresh(20, 4);
	CHECK(t.title[0] == '\0' && !t.title_dirty);
	feed("\033]0;Hello World\007");
	CHECK(strcmp(t.title, "Hello World") == 0 && t.title_dirty);
	t.title_dirty = false;
	/* ST terminator, UTF-8, and control character stripping. */
	feed("\033]2;caf\xc3\xa9\x01\x7f!\033\\");
	CHECK(strcmp(t.title, "caf\xc3\xa9!") == 0 && t.title_dirty);
	/* Empty title resets to empty string (host falls back to default). */
	t.title_dirty = false;
	feed("\033]2;\033\\");
	CHECK(t.title[0] == '\0' && t.title_dirty);
	/* RIS resets title. */
	feed("\033]0;Temp\007");
	CHECK(strcmp(t.title, "Temp") == 0);
	feed("\033c");
	CHECK(t.title[0] == '\0' && t.title_dirty);

	/* --- OSC 7: working directory --- */
	fresh(20, 4);
	CHECK(t.cwd == NULL);
	feed("\033]7;file:///home/fred/my%20dir\033\\");
	CHECK(t.cwd != NULL && strcmp(t.cwd, "/home/fred/my dir") == 0);
	feed("\033]7;FILE://localhost/tmp/sub%2Fdir\007");
	CHECK(t.cwd != NULL && strcmp(t.cwd, "/tmp/sub/dir") == 0);
	/* Remote hostname is rejected (keeps previous cwd). */
	feed("\033]7;file://definitely-remote-host.invalid/etc\033\\");
	CHECK(t.cwd != NULL && strcmp(t.cwd, "/tmp/sub/dir") == 0);
	/* NUL byte (%00), bad percent escape, and non-file scheme rejected. */
	feed("\033]7;file:///tmp/bad%00path\033\\");
	CHECK(strcmp(t.cwd, "/tmp/sub/dir") == 0);
	feed("\033]7;file:///tmp/bad%ZZ\033\\");
	CHECK(strcmp(t.cwd, "/tmp/sub/dir") == 0);
	feed("\033]7;https://example.com/path\033\\");
	CHECK(strcmp(t.cwd, "/tmp/sub/dir") == 0);
	/* Local hostname matches t.hostname. */
	if (t.hostname[0] != '\0') {
		char seq[320];
		snprintf(seq, sizeof(seq), "\033]7;file://%s/var/log\033\\",
			t.hostname);
		feed(seq);
		CHECK(strcmp(t.cwd, "/var/log") == 0);
	}
	/* Empty OSC 7 clears cwd. */
	feed("\033]7;\033\\");
	CHECK(t.cwd == NULL);

	/* --- OSC 8: hyperlinks --- */
	fresh(20, 3);
	feed("\033]8;id=doc1;https://tamlinux.org\033\\\033[31mhi\033[0m!\033]8;;\033\\ plain");
	uint32_t lid0 = tl_term_cell(&t, 0, 0)->link_id;
	CHECK(lid0 != 0);
	CHECK(tl_term_cell(&t, 1, 0)->link_id == lid0);
	/* SGR 0 must NOT close the hyperlink. */
	CHECK(tl_term_cell(&t, 2, 0)->link_id == lid0);
	CHECK(tl_term_cell(&t, 3, 0)->link_id == 0);
	CHECK(strcmp(tl_term_link_uri(&t, 0, 0), "https://tamlinux.org") == 0);
	CHECK(tl_term_link_uri(&t, 4, 0) == NULL);
	/* Reusing the same id= and uri reuses the same link_id. */
	feed("\033]8;foo=bar:id=doc1;https://tamlinux.org\007X\033]8;;\007");
	CHECK(tl_term_cell(&t, 9, 0)->link_id == lid0);
	/* Wide char gets link_id on both head and tail. */
	feed("\033]8;;https://wide.test\033\\\xe6\xbc\xa2\033]8;;\033\\");
	CHECK(tl_term_cell(&t, 10, 0)->link_id != 0 &&
		tl_term_cell(&t, 10, 0)->link_id == tl_term_cell(&t, 11, 0)->link_id);
	/* Link survives scrolling into scrollback. */
	feed("\r\nl1\r\nl2\r\nl3");
	CHECK(t.sb_len == 1);
	CHECK(strcmp(tl_term_link_uri(&t, 0, 0), "https://tamlinux.org") == 0);
	/* Evicting more than TL_LINK_MAX links makes old link_id resolve to NULL safely. */
	for (int i = 0; i < TL_LINK_MAX + 10; i++) {
		char seq[64];
		snprintf(seq, sizeof(seq), "\033]8;;https://ex/%d\033\\a\033]8;;\033\\", i);
		feed(seq);
	}
	CHECK(tl_term_link(&t, lid0) == NULL);

	/* --- OSC 52: clipboard & base64 with policy --- */
	{
		char *enc = NULL, *dec = NULL;
		size_t elen = 0, dlen = 0;
		CHECK(tl_base64_encode("hello!", 6, &enc, &elen) == 0);
		CHECK(elen == 8 && strcmp(enc, "aGVsbG8h") == 0);
		CHECK(tl_base64_decode("aGVs\n bG8h", 10, &dec, &dlen) == 0);
		CHECK(dlen == 6 && memcmp(dec, "hello!", 6) == 0);
		free(enc);
		free(dec);
		/* Invalid base64 rejected. */
		CHECK(tl_base64_decode("!!!", 3, &dec, &dlen) == -1);
		CHECK(tl_base64_decode("abc=d", 5, &dec, &dlen) == -1);
	}
	fresh(20, 4);
	/* Default policy is TL_OSC52_WRITE: write allowed, read denied. */
	CHECK(t.osc52_policy == TL_OSC52_WRITE);
	feed("\033]52;c;aGVsbG8=\033\\");
	CHECK(t.osc52_write_pending && t.osc52_write_clip && !t.osc52_write_prim);
	CHECK(t.osc52_write_len == 5 && memcmp(t.osc52_write_data, "hello", 5) == 0);
	tl_term_osc52_clear_write(&t);
	/* Empty target defaults to clipboard; empty data clears clipboard. */
	feed("\033]52;;\007");
	CHECK(t.osc52_write_pending && t.osc52_write_clip && t.osc52_write_len == 0);
	tl_term_osc52_clear_write(&t);
	/* Read query '?' denied under default write-only policy. */
	feed("\033]52;c;?\033\\");
	CHECK(!t.osc52_read_pending);
	/* Disabled policy denies both write and read. */
	tl_term_set_osc52_policy(&t, TL_OSC52_DISABLED);
	feed("\033]52;c;aGVsbG8=\033\\");
	feed("\033]52;c;?\033\\");
	CHECK(!t.osc52_write_pending && !t.osc52_read_pending);
	/* Read-only policy allows '?' and denies write. */
	tl_term_set_osc52_policy(&t, TL_OSC52_READ);
	feed("\033]52;c;aGVsbG8=\033\\");
	CHECK(!t.osc52_write_pending);
	feed("\033]52;p;?\007");
	CHECK(t.osc52_read_pending && t.osc52_read_prim && !t.osc52_read_st &&
		strcmp(t.osc52_read_target, "p") == 0);
	t.osc52_read_pending = false;
	/* Both policy allows both write and ST-terminated read. */
	tl_term_set_osc52_policy(&t, TL_OSC52_BOTH);
	feed("\033]52;pc;b2s=\033\\");
	CHECK(t.osc52_write_pending && t.osc52_write_clip && t.osc52_write_prim &&
		t.osc52_write_len == 2 && memcmp(t.osc52_write_data, "ok", 2) == 0);
	tl_term_osc52_clear_write(&t);
	feed("\033]52;;?\033\\");
	CHECK(t.osc52_read_pending && !t.osc52_read_prim && t.osc52_read_st &&
		strcmp(t.osc52_read_target, "c") == 0);
	t.osc52_read_pending = false;

	/* --- OSC 4, 10, 11, 12, 104, 110, 111, 112: colors --- */
	{
		uint32_t c = 0;
		CHECK(tl_osc_parse_color("rgb:f/0/a", 9, &c) == 0 && c == 0xff00aa);
		CHECK(tl_osc_parse_color("rgb:12/34/56", 12, &c) == 0 && c == 0x123456);
		CHECK(tl_osc_parse_color("rgb:abc/def/123", 15, &c) == 0 && c == 0xabde12);
		CHECK(tl_osc_parse_color("rgb:1234/5678/9abc", 18, &c) == 0 && c == 0x12569a);
		CHECK(tl_osc_parse_color("#f80", 4, &c) == 0 && c == 0xff8800);
		CHECK(tl_osc_parse_color("#112233", 7, &c) == 0 && c == 0x112233);
		CHECK(tl_osc_parse_color("#123456789", 10, &c) == 0 && c == 0x124578);
		CHECK(tl_osc_parse_color("#aabbccddeeff", 13, &c) == 0 && c == 0xaaccee);
		CHECK(tl_osc_parse_color("rgb:12/34", 9, &c) == -1);
		CHECK(tl_osc_parse_color("#12345", 6, &c) == -1);
	}
	fresh(20, 4);
	struct tl_palette custom;
	tl_palette_default(&custom);
	custom.fg = 0x102030;
	custom.bg = 0x010203;
	custom.cursor = 0xabcdef;
	custom.index[2] = 0x445566;
	tl_term_set_palette(&t, &custom);
	t.reply_len = 0;
	/* Query fg (10) with ST and bg (11) with BEL. */
	feed("\033]10;?\033\\\033]11;?\007");
	const char *want_q =
		"\033]10;rgb:1010/2020/3030\033\\"
		"\033]11;rgb:0101/0202/0303\007";
	CHECK(t.reply_len == strlen(want_q) &&
		memcmp(t.reply, want_q, t.reply_len) == 0);
	t.reply_len = 0;
	/* Multi-spec OSC 10 sets fg, bg, cursor at once. */
	feed("\033]10;#aabbcc;rgb:11/22/33;#0f0\033\\");
	CHECK(t.pal.fg == 0xaabbcc && t.pal.bg == 0x112233 && t.pal.cursor == 0x00ff00);
	/* Multi-pair OSC 4 sets index 2 and queries index 2. */
	feed("\033]4;2;rgb:de/ad/be;2;?\033\\");
	CHECK(t.pal.index[2] == 0xdeadbe);
	const char *want_4 = "\033]4;2;rgb:dede/adad/bebe\033\\";
	CHECK(t.reply_len == strlen(want_4) &&
		memcmp(t.reply, want_4, t.reply_len) == 0);
	t.reply_len = 0;
	/* OSC 104 resets single index or all indices to configured default_pal. */
	feed("\033]4;5;#ffffff\033\\");
	CHECK(t.pal.index[5] == 0xffffff);
	feed("\033]104;2\033\\");
	CHECK(t.pal.index[2] == 0x445566 && t.pal.index[5] == 0xffffff);
	feed("\033]104\033\\");
	CHECK(t.pal.index[5] == custom.index[5]);
	/* OSC 110/111/112 reset fg/bg/cursor to configured default_pal. */
	feed("\033]110\033\\\033]111\033\\\033]112\033\\");
	CHECK(t.pal.fg == 0x102030 && t.pal.bg == 0x010203 && t.pal.cursor == 0xabcdef);

	/* --- VT OSC parser edge cases --- */
	fresh(20, 4);
	/* Split across single-byte feeds. */
	const char *split_osc = "\033]2;split\033\\";
	for (size_t i = 0; split_osc[i] != '\0'; i++)
		tl_term_feed(&t, split_osc + i, 1);
	CHECK(strcmp(t.title, "split") == 0);
	/* ESC followed by '[' inside OSC aborts OSC and executes the CSI. */
	feed("\033]2;aborted\033[2;3HZ");
	CHECK(strcmp(t.title, "split") == 0);
	CHECK(tl_term_cell(&t, 2, 1)->cp == 'Z');
	/* CAN (0x18) aborts OSC. */
	feed("\033]2;never\x18ok");
	CHECK(strcmp(t.title, "split") == 0);
	/* Oversized OSC (> TL_VT_OSC_MAX) is ignored cleanly without crashing. */
	feed("\033]2;");
	char big[4096];
	memset(big, 'A', sizeof(big));
	for (size_t sent = 0; sent < TL_VT_OSC_MAX + 8192; sent += sizeof(big))
		tl_term_feed(&t, big, sizeof(big));
	feed("\007");
	CHECK(strcmp(t.title, "split") == 0);

	/* --- DA2, XTVERSION, DEC 2026 synchronized output, and DECRQM --- */
	fresh(20, 4);
	{
		int maj = 0, min = 0, pat = 0;
		(void)sscanf(TL_VERSION, "%d.%d.%d", &maj, &min, &pat);
		char want_da2[64];
		snprintf(want_da2, sizeof(want_da2), "\033[>1;%d;0c",
			maj * 10000 + min * 100 + pat);
		feed("\033[>c");
		CHECK(t.reply_len == strlen(want_da2) &&
			memcmp(t.reply, want_da2, t.reply_len) == 0);
		t.reply_len = 0;
		feed("\033[>0c");
		CHECK(t.reply_len == strlen(want_da2) &&
			memcmp(t.reply, want_da2, t.reply_len) == 0);
		t.reply_len = 0;
		/* Non-zero DA2 parameter is ignored. */
		feed("\033[>1c");
		CHECK(t.reply_len == 0);

		char want_xt[64];
		snprintf(want_xt, sizeof(want_xt), "\033P>|tam-last(%s)\033\\",
			TL_VERSION);
		feed("\033[>q");
		CHECK(t.reply_len == strlen(want_xt) &&
			memcmp(t.reply, want_xt, t.reply_len) == 0);
		t.reply_len = 0;
		feed("\033[>0q");
		CHECK(t.reply_len == strlen(want_xt) &&
			memcmp(t.reply, want_xt, t.reply_len) == 0);
		t.reply_len = 0;
		feed("\033[>1q");
		CHECK(t.reply_len == 0);
	}

	/* DEC 2026 synchronized output + RIS reset. */
	CHECK(!t.sync_output);
	feed("\033[?2026h");
	CHECK(t.sync_output);
	feed("\033[?2026l");
	CHECK(!t.sync_output);
	feed("\033[?2026h\033c");
	CHECK(!t.sync_output);

	/* DECRQM for ANSI modes (4 = IRM, 99 = unknown). */
	t.reply_len = 0;
	feed("\033[4$p");
	CHECK(t.reply_len == 7 && memcmp(t.reply, "\033[4;2$y", 7) == 0);
	t.reply_len = 0;
	feed("\033[4h\033[4$p");
	CHECK(t.reply_len == 7 && memcmp(t.reply, "\033[4;1$y", 7) == 0);
	t.reply_len = 0;
	feed("\033[4l\033[99$p");
	CHECK(t.reply_len == 8 && memcmp(t.reply, "\033[99;0$y", 8) == 0);

	/* DECRQM for DEC private modes (defaults, toggled, and unknown). */
	t.reply_len = 0;
	feed("\033[?1$p\033[?6$p\033[?7$p\033[?25$p\033[?2004$p\033[?2026$p\033[?9999$p");
	const char *want_decrqm_def =
		"\033[?1;2$y"
		"\033[?6;2$y"
		"\033[?7;1$y"
		"\033[?25;1$y"
		"\033[?2004;2$y"
		"\033[?2026;2$y"
		"\033[?9999;0$y";
	CHECK(t.reply_len == strlen(want_decrqm_def) &&
		memcmp(t.reply, want_decrqm_def, t.reply_len) == 0);

	t.reply_len = 0;
	feed("\033[?1h\033[?6h\033[?7l\033[?25l\033[?1049h\033[?1002h\033[?1006h\033[?2004h\033[?2026h");
	feed("\033[?1$p\033[?6$p\033[?7$p\033[?25$p\033[?47$p\033[?1047$p\033[?1049$p"
	     "\033[?1000$p\033[?1002$p\033[?1003$p\033[?1006$p\033[?2004$p\033[?2026$p");
	const char *want_decrqm_on =
		"\033[?1;1$y"
		"\033[?6;1$y"
		"\033[?7;2$y"
		"\033[?25;2$y"
		"\033[?47;1$y"
		"\033[?1047;1$y"
		"\033[?1049;1$y"
		"\033[?1000;2$y"
		"\033[?1002;1$y"
		"\033[?1003;2$y"
		"\033[?1006;1$y"
		"\033[?2004;1$y"
		"\033[?2026;1$y";
	CHECK(t.reply_len == strlen(want_decrqm_on) &&
		memcmp(t.reply, want_decrqm_on, t.reply_len) == 0);

	/* OSC 777 desktop notifications. */
	feed("\033c");
	CHECK(!t.notify.pending);
	feed("\033]777;notify;Build done;0 errors, 2 warnings\033\\");
	CHECK(t.notify.pending);
	CHECK(t.notify.urgency == TL_NOTIFY_NORMAL);
	CHECK(!t.notify.only_unfocused);
	CHECK(strcmp(t.notify.title, "Build done") == 0);
	CHECK(strcmp(t.notify.body, "0 errors, 2 warnings") == 0);
	tl_term_notify_clear(&t);
	CHECK(!t.notify.pending);
	/* Non-notify OSC 777 and empty payload are ignored. */
	feed("\033]777;other;foo;bar\007");
	CHECK(!t.notify.pending);
	feed("\033]777;notify;;\007");
	CHECK(!t.notify.pending);
	/* Control characters stripped from title and body. */
	feed("\033]777;notify;Hi\001\nThere;Ok\002!\033\\");
	CHECK(t.notify.pending);
	CHECK(strcmp(t.notify.title, "HiThere") == 0);
	CHECK(strcmp(t.notify.body, "Ok!") == 0);
	tl_term_notify_clear(&t);

	/* OSC 99 desktop notifications (single-shot, chunked, base64, urgency, occasion). */
	feed("\033]99;;Simple title\033\\");
	CHECK(t.notify.pending);
	CHECK(t.notify.urgency == TL_NOTIFY_NORMAL);
	CHECK(!t.notify.only_unfocused);
	CHECK(strcmp(t.notify.title, "Simple title") == 0);
	CHECK(t.notify.body[0] == '\0');
	tl_term_notify_clear(&t);

	feed("\033]99;i=job1:p=title:d=0:u=2:o=unfocused;Alert\033\\");
	CHECK(!t.notify.pending);
	feed("\033]99;i=job1:p=body:d=0;Line 1\033\\");
	CHECK(!t.notify.pending);
	/* "\nLine 2" in base64 = CkxpbmUgMg== */
	feed("\033]99;i=job1:p=body:d=1:e=1;CkxpbmUgMg==\033\\");
	CHECK(t.notify.pending);
	CHECK(t.notify.urgency == TL_NOTIFY_CRITICAL);
	CHECK(t.notify.only_unfocused);
	CHECK(strcmp(t.notify.title, "Alert") == 0);
	CHECK(strcmp(t.notify.body, "Line 1\nLine 2") == 0);
	/* RIS clears pending notification and partial accumulator. */
	feed("\033]99;i=job2:p=title:d=0;Partial\033\\");
	feed("\033c");
	CHECK(!t.notify.pending);
	CHECK(!t.notify_acc.active);

	/* Garbage never breaks the invariants. */
	srand(1);
	unsigned char junk[4096];
	for (int round = 0; round < 200; round++) {
		for (size_t i = 0; i < sizeof(junk); i++) {
			int r = rand();
			/* Mostly escape-ish bytes, so the parser's states get used. */
			junk[i] = (unsigned char)(r % 4 == 0 ? "\033[;?>$0123m]Ppq\\c\007"[r % 18] : r);
		}
		tl_term_feed(&t, junk, sizeof(junk));
		CHECK(t.cx >= 0 && t.cx < t.cols && t.cy >= 0 && t.cy < t.rows);
		t.reply_len = 0;
	}

	tl_term_free(&t);
	CHECK_DONE("test_vt");
}
