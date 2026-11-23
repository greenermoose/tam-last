/* Incremental search across scrollback and the live screen. Pure logic:
 * no Wayland, no pty, so it is unit-tested directly against struct tl_term. */
#ifndef TL_SEARCH_H
#define TL_SEARCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tam-last/term.h"

#define TL_SEARCH_QUERY_MAX 256
#define TL_SEARCH_MAX_MATCHES 8192

struct tl_search_match {
	int abs_y;
	int x0;
	int x1; /* inclusive column index */
};

struct tl_search {
	bool active;
	char query[TL_SEARCH_QUERY_MAX];
	size_t query_len;
	struct tl_search_match *matches;
	size_t match_count;
	size_t match_cap;
	int current; /* index into matches, or -1 if none */
	int saved_view_offset;
};

/* Enter search mode, saving the current view_offset for Esc cancellation. */
void tl_search_start(struct tl_search *s, const struct tl_term *t);
/* Exit search mode and release match storage. */
void tl_search_end(struct tl_search *s);
void tl_search_free(struct tl_search *s);

/* Edit the UTF-8 query and recompute matches against t. */
void tl_search_push_cp(struct tl_search *s, const struct tl_term *t,
	uint32_t cp);
void tl_search_push_utf8(struct tl_search *s, const struct tl_term *t,
	const char *utf8, size_t len);
void tl_search_pop_cp(struct tl_search *s, const struct tl_term *t);
void tl_search_pop_word(struct tl_search *s, const struct tl_term *t);
void tl_search_clear_query(struct tl_search *s, const struct tl_term *t);
void tl_search_refresh(struct tl_search *s, const struct tl_term *t);

/* Step to an older (-1) or newer (+1) match, wrapping around. */
void tl_search_next(struct tl_search *s, int delta);

/* Scroll t->view_offset so the current match is visible (primary screen). */
void tl_search_sync_view(const struct tl_search *s, struct tl_term *t);

/* Current match pointer, or NULL when inactive / no matches. */
const struct tl_search_match *tl_search_current(const struct tl_search *s);

/* Highlight state for cell (x, abs_y): 0 = none, 1 = match, 2 = current. */
int tl_search_cell_state(const struct tl_search *s, int x, int abs_y);

/* Format the bottom status bar text into buf. */
void tl_search_format_bar(const struct tl_search *s, char *buf, size_t cap);

#endif
