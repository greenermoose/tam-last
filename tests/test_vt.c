/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Bytes in, cells out: the parser and the screen together. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
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

	/* Garbage never breaks the invariants. */
	srand(1);
	unsigned char junk[4096];
	for (int round = 0; round < 200; round++) {
		for (size_t i = 0; i < sizeof(junk); i++) {
			int r = rand();
			/* Mostly escape-ish bytes, so the parser's states get used. */
			junk[i] = (unsigned char)(r % 4 == 0 ? "\033[;?0123m]P\\\007"[r % 13] : r);
		}
		tl_term_feed(&t, junk, sizeof(junk));
		CHECK(t.cx >= 0 && t.cx < t.cols && t.cy >= 0 && t.cy < t.rows);
		t.reply_len = 0;
	}

	tl_term_free(&t);
	CHECK_DONE("test_vt");
}
