/* Unit tests for 0.2.2 interactive modes and bounded actions:
 * scrollback search, keyboard URL mode, and notify-send deadline handling. */
#include "check.h"
#include "tam-last/action.h"
#include "tam-last/search.h"
#include "tam-last/term.h"
#include "tam-last/url.h"

#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void feed(struct tl_term *t, const char *s)
{
	tl_term_feed(t, s, strlen(s));
}

static void test_search_scrollback(void)
{
	struct tl_term t;
	CHECK(tl_term_init(&t, 40, 5) == 0);
	/* Push 12 lines so early lines enter scrollback. */
	feed(&t, "alpha one\r\n");
	feed(&t, "beta two\r\n");
	feed(&t, "Alpha three\r\n");
	feed(&t, "gamma four\r\n");
	feed(&t, "delta five\r\n");
	feed(&t, "epsilon six\r\n");
	feed(&t, "ALPHA seven\r\n");
	feed(&t, "zeta eight\r\n");
	feed(&t, "caf\xc3\xa9 nine\r\n");
	feed(&t, "wide \xe6\x97\xa5\xe6\x9c\xac ten\r\n");
	feed(&t, "alpha eleven\r\n");
	feed(&t, "last line");
	CHECK(t.sb_len >= 7);

	struct tl_search s;
	memset(&s, 0, sizeof(s));
	tl_search_start(&s, &t);
	CHECK(s.active);
	CHECK(s.saved_view_offset == 0);

	/* Lowercase query: smart-case insensitive, matches all 4 "alpha" variants. */
	tl_search_push_utf8(&s, &t, "alpha", 5);
	CHECK(s.match_count == 4);
	CHECK(s.current == 3); /* newest match (backward search) */
	const struct tl_search_match *m = tl_search_current(&s);
	CHECK(m != NULL);
	CHECK(m->x0 == 0 && m->x1 == 4);
	CHECK(tl_search_cell_state(&s, 0, m->abs_y) == 2);
	CHECK(tl_search_cell_state(&s, 4, m->abs_y) == 2);
	CHECK(tl_search_cell_state(&s, 5, m->abs_y) == 0);
	CHECK(tl_search_cell_state(&s, 0, s.matches[0].abs_y) == 1);

	/* Wrap-around navigation and viewport sync into scrollback. */
	tl_search_next(&s, -1);
	CHECK(s.current == 2);
	tl_search_next(&s, -1);
	CHECK(s.current == 1);
	tl_search_next(&s, -1);
	CHECK(s.current == 0);
	tl_search_sync_view(&s, &t);
	CHECK(t.view_offset > 0);
	tl_search_next(&s, -1);
	CHECK(s.current == 3); /* wrapped to newest */
	tl_search_next(&s, 1);
	CHECK(s.current == 0); /* wrapped to oldest */

	/* Smart-case sensitive once uppercase is present. */
	tl_search_clear_query(&s, &t);
	CHECK(s.match_count == 0);
	tl_search_push_utf8(&s, &t, "Alpha", 5);
	CHECK(s.match_count == 1);

	/* UTF-8 accented character and backspace popping multi-byte codepoint. */
	tl_search_clear_query(&s, &t);
	tl_search_push_utf8(&s, &t, "caf\xc3\xa9", 5);
	CHECK(s.match_count == 1);
	tl_search_pop_cp(&s, &t);
	CHECK(strcmp(s.query, "caf") == 0);
	CHECK(s.match_count == 1);

	/* Wide characters: x1 covers the wide tail cell. */
	tl_search_clear_query(&s, &t);
	tl_search_push_utf8(&s, &t, "\xe6\x97\xa5\xe6\x9c\xac", 6);
	CHECK(s.match_count == 1);
	m = tl_search_current(&s);
	CHECK(m != NULL);
	CHECK(m->x0 == 5 && m->x1 == 8); /* 2 wide chars = 4 columns (5..8) */

	/* Ctrl+W word delete and bar formatting. */
	tl_search_clear_query(&s, &t);
	tl_search_push_utf8(&s, &t, "alpha one", 9);
	CHECK(s.match_count == 1);
	char bar[256];
	tl_search_format_bar(&s, bar, sizeof(bar));
	CHECK(strstr(bar, "[1/1]") != NULL);
	tl_search_pop_word(&s, &t);
	CHECK(strcmp(s.query, "alpha ") == 0);

	tl_search_free(&s);
	tl_term_free(&t);
}

static void test_url_mode(void)
{
	/* URI safety checks. */
	CHECK(tl_url_is_safe("https://example.com/path?a=1&b=2"));
	CHECK(tl_url_is_safe("http://localhost:8080/test"));
	CHECK(tl_url_is_safe("mailto:fred@example.org"));
	CHECK(tl_url_is_safe("file:///home/fred/notes.md"));
	CHECK(!tl_url_is_safe("javascript:alert(1)"));
	CHECK(!tl_url_is_safe("-evil-flag"));
	CHECK(!tl_url_is_safe("https://"));
	CHECK(!tl_url_is_safe("https://example.com/a\nb"));
	CHECK(!tl_url_is_safe("https://example.com/a b"));
	CHECK(!tl_url_is_safe(""));
	CHECK(!tl_url_is_safe(NULL));

	struct tl_term t;
	CHECK(tl_term_init(&t, 80, 8) == 0);
	feed(&t, "See (https://example.com/docs.) and https://en.wikipedia.org/wiki/Foo_(bar)!\r\n");
	feed(&t, "Link: \033]8;;https://tamlinux.org/last\033\\tam-last\033]8;;\033\\ end\r\n");
	feed(&t, "Mail: mailto:hi@example.org, unsafe: \033]8;;javascript:bad\033\\nope\033]8;;\033\\\r\n");

	struct tl_url_mode u;
	memset(&u, 0, sizeof(u));
	size_t n = tl_url_start(&u, &t);
	CHECK(u.active);
	CHECK(n == 4);
	CHECK(u.label_len == 1);
	CHECK(strcmp(u.items[0].uri, "https://example.com/docs") == 0);
	CHECK(strcmp(u.items[1].uri, "https://en.wikipedia.org/wiki/Foo_(bar)") == 0);
	CHECK(strcmp(u.items[2].uri, "https://tamlinux.org/last") == 0);
	CHECK(strcmp(u.items[3].uri, "mailto:hi@example.org") == 0);

	/* Matching a 1-char label (case-insensitive). */
	const char *uri = NULL;
	CHECK(tl_url_feed(&u, 'z', &uri) == TL_URL_IGNORED);
	CHECK(tl_url_feed(&u, (char)(u.items[2].label[0] - 'a' + 'A'), &uri) == TL_URL_MATCHED);
	CHECK(uri != NULL && strcmp(uri, "https://tamlinux.org/last") == 0);

	tl_url_end(&u);
	CHECK(!u.active);
	tl_term_free(&t);

	/* More than 14 URLs -> 2-character prefix-free labels. */
	CHECK(tl_term_init(&t, 80, 20) == 0);
	for (int i = 0; i < 16; i++) {
		char line[64];
		snprintf(line, sizeof(line), "https://ex.org/%d\r\n", i);
		feed(&t, line);
	}
	n = tl_url_start(&u, &t);
	CHECK(n == 16);
	CHECK(u.label_len == 2);
	uri = NULL;
	CHECK(tl_url_feed(&u, u.items[15].label[0], &uri) == TL_URL_PENDING);
	CHECK(u.typed_len == 1);
	tl_url_pop(&u);
	CHECK(u.typed_len == 0);
	CHECK(tl_url_feed(&u, u.items[15].label[0], &uri) == TL_URL_PENDING);
	CHECK(tl_url_feed(&u, u.items[15].label[1], &uri) == TL_URL_MATCHED);
	CHECK(uri != NULL && strcmp(uri, "https://ex.org/15") == 0);

	tl_url_end(&u);
	tl_term_free(&t);
}

static void test_action_deadline(const char *outdir)
{
	struct tl_actions act;
	tl_actions_init(&act);
	CHECK(tl_action_next_timeout_ms(&act) == -1);

	/* Empty notification is rejected. */
	struct tl_notify empty;
	memset(&empty, 0, sizeof(empty));
	CHECK(tl_action_notify(&act, &empty, "/bin/true", 500) == -1);

	struct tl_notify n;
	memset(&n, 0, sizeof(n));
	n.pending = true;
	n.urgency = TL_NOTIFY_NORMAL;
	snprintf(n.title, sizeof(n.title), "hello");
	snprintf(n.body, sizeof(n.body), "world");

	/* Fast child exits and is reaped cleanly. */
	CHECK(tl_action_notify(&act, &n, "/bin/true", 500) == 0);
	CHECK(tl_action_next_timeout_ms(&act) >= 0);
	struct timespec ts = { 0, 60 * 1000000L };
	nanosleep(&ts, NULL);
	tl_action_reap(&act);
	CHECK(tl_action_next_timeout_ms(&act) == -1);

	/* Hanging child is terminated promptly when its deadline expires,
	 * and concurrency is capped at TL_ACTION_MAX_PROCS. */
	char hang_path[512];
	snprintf(hang_path, sizeof(hang_path), "%s/test_hang.sh", outdir);
	FILE *f = fopen(hang_path, "w");
	CHECK(f != NULL);
	fputs("#!/bin/sh\nsleep 10\n", f);
	fclose(f);
	CHECK(chmod(hang_path, 0755) == 0);

	for (int i = 0; i < TL_ACTION_MAX_PROCS; i++)
		CHECK(tl_action_notify(&act, &n, hang_path, 80) == 0);
	/* 5th concurrent notification must be dropped safely. */
	CHECK(tl_action_notify(&act, &n, hang_path, 80) == -1);

	struct timespec wait_exp = { 0, 140 * 1000000L };
	nanosleep(&wait_exp, NULL);
	CHECK(tl_action_check_deadlines(&act) == TL_ACTION_MAX_PROCS);
	CHECK(tl_action_next_timeout_ms(&act) == -1);
	unlink(hang_path);

	/* Unsafe URL is rejected before fork. */
	CHECK(tl_action_open_url("javascript:alert(1)") == -1);
	CHECK(tl_action_open_url("-bad") == -1);

	tl_actions_kill_all(&act);
}

int main(int argc, char **argv)
{
	setlocale(LC_CTYPE, "C.UTF-8");
	const char *outdir = argc > 1 ? argv[1] : "/tmp";
	test_search_scrollback();
	test_url_mode();
	test_action_deadline(outdir);
	CHECK_DONE("test_modes");
}
