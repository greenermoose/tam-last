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
#define TL_TITLE_MAX 512
#define TL_CWD_MAX 4096
#define TL_LINK_MAX 256
#define TL_LINK_URI_MAX 2048
#define TL_REPLY_MAX 4096
#define TL_NOTIFY_TITLE_MAX 256
#define TL_NOTIFY_BODY_MAX 2048
#define TL_NOTIFY_ID_MAX 64

enum tl_notify_urgency {
	TL_NOTIFY_LOW = 0,
	TL_NOTIFY_NORMAL = 1,
	TL_NOTIFY_CRITICAL = 2
};

struct tl_notify {
	bool pending;
	bool only_unfocused;
	enum tl_notify_urgency urgency;
	char title[TL_NOTIFY_TITLE_MAX];
	char body[TL_NOTIFY_BODY_MAX];
};

struct tl_notify_acc {
	bool active;
	bool only_unfocused;
	enum tl_notify_urgency urgency;
	char id[TL_NOTIFY_ID_MAX];
	char title[TL_NOTIFY_TITLE_MAX];
	size_t title_len;
	char body[TL_NOTIFY_BODY_MAX];
	size_t body_len;
};

struct tl_palette {
	uint32_t fg, bg, cursor, cursor_text;
	uint32_t index[256];
};

/* The compiled-in colors: Tamlinux's current dark theme (Tokyo Night) for
 * the first 16, then the standard 6x6x6 cube and gray ramp. */
void tl_palette_default(struct tl_palette *p);
uint32_t tl_color_resolve(const struct tl_palette *p, uint32_t color,
	bool fg);

enum tl_osc52_policy {
	TL_OSC52_WRITE = 0, /* default: write-only (copy allowed, read denied) */
	TL_OSC52_DISABLED,  /* neither read nor write */
	TL_OSC52_READ,      /* read-only */
	TL_OSC52_BOTH       /* read and write */
};

struct tl_link {
	uint32_t id;   /* non-zero monotonic id; 0 = unused slot */
	char *uri;     /* validated URI string */
	char *user_id; /* optional id= parameter from OSC 8, or NULL */
};

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
	uint32_t link_id; /* 0 = no OSC 8 hyperlink */
};

struct tl_pen {
	uint32_t fg, bg;
	uint16_t attrs;
	uint32_t link_id;
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
	bool sync_output; /* 2026: synchronized update (BSU/ESU) */
	uint8_t kitty_kbd[2][TL_KITTY_KBD_STACK_MAX]; /* 0 = primary, 1 = alt */
	int kitty_kbd_top[2];
	struct tl_sb_page sb_pages[TL_SB_RING_CAP];
	int sb_head;
	int sb_count;
	int sb_len; /* total rows in scrollback */
	int view_offset; /* 0 = live bottom, >0 = scrolled up */
	/* Dynamic palette (OSC 4, 10, 11, 12, 104, 110-112). */
	struct tl_palette pal;
	struct tl_palette default_pal;
	/* Window title (OSC 0, 2) and working directory (OSC 7). */
	char title[TL_TITLE_MAX];
	bool title_dirty;
	char *cwd;
	char hostname[256];
	/* Hyperlink table (OSC 8). */
	struct tl_link links[TL_LINK_MAX];
	uint32_t next_link_id;
	uint32_t link_cursor;
	uint32_t hover_link_id;
	/* Clipboard (OSC 52) policy and pending requests for the host loop. */
	enum tl_osc52_policy osc52_policy;
	bool osc52_write_pending;
	bool osc52_write_clip;
	bool osc52_write_prim;
	char *osc52_write_data;
	size_t osc52_write_len;
	bool osc52_read_pending;
	bool osc52_read_prim;
	bool osc52_read_st;
	char osc52_read_target[16];
	/* Desktop notifications (OSC 777 and OSC 99). */
	struct tl_notify notify;
	struct tl_notify_acc notify_acc;
	bool dirty;
	bool full_dirty;
	bool *dirty_rows;
	/* Answers to queries (DSR, DA, OSC 4/10/11/12), for the caller to write to the pty. */
	unsigned char reply[TL_REPLY_MAX];
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
/* Set both the active and default palette (used by OSC 104 / 110-112 / RIS). */
void tl_term_set_palette(struct tl_term *t, const struct tl_palette *pal);
void tl_term_set_osc52_policy(struct tl_term *t, enum tl_osc52_policy policy);
/* Append bytes to the PTY reply buffer (used by term.c and osc.c). */
void tl_term_reply(struct tl_term *t, const void *data, size_t len);
void tl_term_mark_all(struct tl_term *t);
/* Clear a consumed OSC 52 write request. */
void tl_term_osc52_clear_write(struct tl_term *t);
/* Clear a consumed desktop notification request. */
void tl_term_notify_clear(struct tl_term *t);
/* Look up an OSC 8 hyperlink by id or cell coordinate. */
const struct tl_link *tl_term_link(const struct tl_term *t, uint32_t link_id);
const char *tl_term_link_uri(const struct tl_term *t, int x, int abs_y);

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
