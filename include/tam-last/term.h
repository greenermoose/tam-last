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

enum tl_cursor_style {
	TL_CURSOR_BLOCK = 0,
	TL_CURSOR_UNDERLINE,
	TL_CURSOR_BAR
};

enum tl_mouse_mode {
	TL_MOUSE_NONE = 0,
	TL_MOUSE_X10 = 1000,
	TL_MOUSE_DRAG = 1002,
	TL_MOUSE_ANY = 1003
};

#define TL_SB_PAGE_ROWS 64
#define TL_SB_MAX_ROWS 4096
#define TL_SB_RING_CAP 128
#define TL_KITTY_KBD_STACK_MAX 16

struct tl_sb_page {
	struct tl_cell *cells;
	int cols;
	int used;
};

/* The right half of a wide character. */
#define TL_CELL_WIDE_TAIL 0xFFFFFFFFu
#define TL_CELL_COMB_MAX 2

struct tl_cell {
	uint32_t cp; /* ' ' when blank */
	uint32_t comb[TL_CELL_COMB_MAX];
	uint32_t fg, bg;
	uint16_t attrs;
	uint8_t ncomb;
};

struct tl_pen {
	uint32_t fg, bg;
	uint16_t attrs;
};

struct tl_saved {
	int cx, cy;
	bool wrap_next;
	bool origin;
	bool dec_gfx;
	struct tl_pen pen;
};

struct tl_term {
	int cols, rows;
	struct tl_cell *cells;
	struct tl_cell *alt_cells;
	bool alt_screen;
	int cx, cy;
	int scroll_top, scroll_bottom;
	bool wrap_next; /* the last column was written; wrap before the next */
	bool origin_mode;
	bool insert_mode;
	bool dec_gfx;
	enum tl_cursor_style cursor_style;
	struct tl_pen pen;
	struct tl_saved saved;
	struct tl_saved alt_saved;
	bool cursor_visible;
	bool autowrap;
	bool app_cursor; /* DECCKM: cursor keys send SS3 */
	bool app_keypad; /* DECKPAM */
	enum tl_mouse_mode mouse_mode; /* 1000, 1002, 1003 */
	bool mouse_sgr; /* 1006 */
	bool bracketed_paste; /* 2004 */
	uint8_t kitty_kbd[2][TL_KITTY_KBD_STACK_MAX]; /* 0 = primary, 1 = alt */
	int kitty_kbd_top[2];
	struct tl_sb_page sb_pages[TL_SB_RING_CAP];
	int sb_head;
	int sb_count;
	int sb_len; /* total rows in scrollback */
	int view_offset; /* 0 = live bottom, >0 = scrolled up */
	bool dirty;
	bool full_dirty;
	bool *dirty_rows;
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
/* Active kitty keyboard protocol flags for the current screen. */
unsigned tl_term_kitty_kbd_flags(const struct tl_term *t);

/* Scrollback navigation (primary screen only). */
void tl_term_scroll_view(struct tl_term *t, int delta);
void tl_term_reset_view(struct tl_term *t);

/* Row pointer for an absolute line index (0 = oldest scrollback row,
 * sb_len .. sb_len + rows - 1 = live screen). Sets *out_cols. */
const struct tl_cell *tl_term_abs_row(const struct tl_term *t, int abs_y,
	int *out_cols);
/* Row pointer for visible row y (0..rows-1) accounting for view_offset. */
const struct tl_cell *tl_term_view_row(const struct tl_term *t, int y,
	int *out_cols);

static inline struct tl_cell *tl_term_cell(const struct tl_term *t, int x,
	int y)
{
	return &t->cells[(size_t)y * (size_t)t->cols + (size_t)x];
}

#endif
