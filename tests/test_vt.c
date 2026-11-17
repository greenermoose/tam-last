/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Bytes in, cells out: the parser and the screen together. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
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
