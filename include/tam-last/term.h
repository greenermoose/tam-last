/* The terminal's screen: a grid of cells, the cursor, the drawing pen and
 * the modes programs set. Pure logic: no Wayland, no pty, so it is tested
 * by feeding bytes and reading cells back. */
#ifndef TL_TERM_H
#define TL_TERM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tam-last/vt.h"

/* Colors: 0 is the default; 0x01000000 | n is palette entry n (0..255);
 * 0x02000000 | 0xRRGGBB is direct color. */
#define TL_COLOR_DEFAULT 0u
#define TL_COLOR_INDEX(n) (0x01000000u | (uint32_t)(n))
#define TL_COLOR_RGB(rgb) (0x02000000u | ((uint32_t)(rgb) & 0xffffffu))

enum {
	TL_ATTR_BOLD = 1 << 0,
	TL_ATTR_DIM = 1 << 1,
	TL_ATTR_ITALIC = 1 << 2,
	TL_ATTR_UNDERLINE = 1 << 3,
	TL_ATTR_BLINK = 1 << 4,
	TL_ATTR_INVERSE = 1 << 5,
	TL_ATTR_HIDDEN = 1 << 6,
	TL_ATTR_STRIKE = 1 << 7
};

/* The right half of a wide character. */
#define TL_CELL_WIDE_TAIL 0xFFFFFFFFu

struct tl_cell {
	uint32_t cp; /* ' ' when blank */
	uint32_t fg, bg;
	uint16_t attrs;
};

struct tl_pen {
	uint32_t fg, bg;
	uint16_t attrs;
};

struct tl_term {
	int cols, rows;
	struct tl_cell *cells;
	int cx, cy;
	bool wrap_next; /* the last column was written; wrap before the next */
	struct tl_pen pen;
	struct {
		int cx, cy;
		bool wrap_next;
		struct tl_pen pen;
	} saved;
	bool cursor_visible;
	bool autowrap;
	bool app_cursor; /* DECCKM: cursor keys send SS3 */
	bool app_keypad; /* DECKPAM */
	bool dirty;
	/* Answers to queries (DSR, DA), for the caller to write to the pty. */
	unsigned char reply[256];
	size_t reply_len;
	struct tl_vt vt;
};

/* Returns 0, or -1 when out of memory or the size is out of range. */
int tl_term_init(struct tl_term *t, int cols, int rows);
void tl_term_free(struct tl_term *t);
/* Keeps what fits, and keeps the cursor's row on screen. */
int tl_term_resize(struct tl_term *t, int cols, int rows);
/* Feed what the program wrote. */
void tl_term_feed(struct tl_term *t, const void *data, size_t len);

static inline struct tl_cell *tl_term_cell(const struct tl_term *t, int x,
	int y)
{
	return &t->cells[(size_t)y * (size_t)t->cols + (size_t)x];
}

#endif
