/* Text selection over the terminal screen and scrollback: drag, word
 * and line selection, and UTF-8 extraction. Pure logic, no Wayland. */
#ifndef TL_SELECT_H
#define TL_SELECT_H

#include <stdbool.h>
#include <stddef.h>

#include "tam-last/term.h"

enum tl_sel_kind {
	TL_SEL_CHAR = 0,
	TL_SEL_WORD,
	TL_SEL_LINE
};

struct tl_sel {
	bool active;
	bool dragging;
	enum tl_sel_kind kind;
	int ax, ay; /* anchor (col, abs_y) */
	int bx, by; /* end (col, abs_y) */
	/* Normalized inclusive range [sx, sy] .. [ex, ey] for fast queries. */
	int sx, sy;
	int ex, ey;
};

void tl_sel_start(struct tl_sel *s, const struct tl_term *t, int x, int abs_y,
	enum tl_sel_kind kind);
void tl_sel_update(struct tl_sel *s, const struct tl_term *t, int x, int abs_y);
void tl_sel_end(struct tl_sel *s);
void tl_sel_clear(struct tl_sel *s);
bool tl_sel_contains(const struct tl_sel *s, int x, int abs_y);

/* Newly allocated NUL-terminated UTF-8 string, or NULL when empty/OOM. */
char *tl_sel_extract(const struct tl_term *t, const struct tl_sel *s,
	size_t *out_len);

#endif
