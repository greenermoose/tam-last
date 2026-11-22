/* What each control, escape and CSI sequence does to the screen. The
 * meanings follow xterm's ctlseqs and the DEC manuals; where they differ,
 * xterm wins, since TERM says xterm-256color. */
#include "tam-last/term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tam_uni.h>

#define TERM_MAX_DIM 4096

static struct tl_cell blank(const struct tl_term *t)
{
	/* Erasing uses the current background (xterm's bce). */
	return (struct tl_cell){ ' ', { 0, 0 }, TL_COLOR_DEFAULT, t->pen.bg, 0, 0 };
}

static void mark_row(struct tl_term *t, int y)
{
	if (y >= 0 && y < t->rows && t->dirty_rows != NULL) {
		t->dirty_rows[y] = true;
		t->dirty = true;
	}
}

static void mark_rows(struct tl_term *t, int y0, int y1)
{
	if (y0 < 0)
		y0 = 0;
	if (y1 > t->rows)
		y1 = t->rows;
	for (int y = y0; y < y1; y++)
		mark_row(t, y);
}

static void mark_all(struct tl_term *t)
{
	t->full_dirty = true;
	t->dirty = true;
	mark_rows(t, 0, t->rows);
}

static void fill(struct tl_term *t, int x0, int y, int x1)
{
	struct tl_cell b = blank(t);
	if (y < 0 || y >= t->rows)
		return;
	if (x0 < 0)
		x0 = 0;
	if (x1 > t->cols)
		x1 = t->cols;
	for (int x = x0; x < x1; x++)
		*tl_term_cell(t, x, y) = b;
	mark_row(t, y);
}

static void fill_rows(struct tl_term *t, int y0, int y1)
{
	if (y0 < 0)
		y0 = 0;
	if (y1 > t->rows)
		y1 = t->rows;
	for (int y = y0; y < y1; y++)
		fill(t, 0, y, t->cols);
}

static void clear_grid(struct tl_cell *cells, int cols, int rows)
{
	struct tl_cell b = { ' ', { 0, 0 }, TL_COLOR_DEFAULT, TL_COLOR_DEFAULT, 0, 0 };
	size_t n = (size_t)cols * (size_t)rows;
	for (size_t i = 0; i < n; i++)
		cells[i] = b;
}

static void sb_clear(struct tl_term *t)
{
	for (int i = 0; i < t->sb_count; i++) {
		int idx = (t->sb_head + i) % TL_SB_RING_CAP;
		free(t->sb_pages[idx].cells);
		t->sb_pages[idx].cells = NULL;
		t->sb_pages[idx].cols = 0;
		t->sb_pages[idx].used = 0;
	}
	t->sb_head = 0;
	t->sb_count = 0;
	t->sb_len = 0;
	t->view_offset = 0;
}

static void sb_push_row(struct tl_term *t, const struct tl_cell *row)
{
	struct tl_sb_page *tail = NULL;
	if (t->sb_count > 0) {
		int idx = (t->sb_head + t->sb_count - 1) % TL_SB_RING_CAP;
		tail = &t->sb_pages[idx];
	}
	if (tail == NULL || tail->used >= TL_SB_PAGE_ROWS || tail->cols != t->cols) {
		if (t->sb_count == TL_SB_RING_CAP) {
			struct tl_sb_page *old = &t->sb_pages[t->sb_head];
			t->sb_len -= old->used;
			free(old->cells);
			old->cells = NULL;
			t->sb_head = (t->sb_head + 1) % TL_SB_RING_CAP;
			t->sb_count--;
		}
		int idx = (t->sb_head + t->sb_count) % TL_SB_RING_CAP;
		tail = &t->sb_pages[idx];
		tail->cells = malloc((size_t)TL_SB_PAGE_ROWS * (size_t)t->cols *
			sizeof(*tail->cells));
		if (tail->cells == NULL)
			return;
		tail->cols = t->cols;
		tail->used = 0;
		t->sb_count++;
	}
	memcpy(tail->cells + (size_t)tail->used * (size_t)tail->cols, row,
		(size_t)t->cols * sizeof(*row));
	tail->used++;
	t->sb_len++;
	if (t->view_offset > 0)
		t->view_offset++;
	/* Evict oldest page once total exceeds TL_SB_MAX_ROWS. */
	while (t->sb_count > 0) {
		struct tl_sb_page *old = &t->sb_pages[t->sb_head];
		if (t->sb_len - old->used < TL_SB_MAX_ROWS)
			break;
		t->sb_len -= old->used;
		free(old->cells);
		old->cells = NULL;
		t->sb_head = (t->sb_head + 1) % TL_SB_RING_CAP;
		t->sb_count--;
	}
	if (t->view_offset > t->sb_len)
		t->view_offset = t->sb_len;
}

static void reset(struct tl_term *t)
{
	if (t->alt_screen) {
		struct tl_cell *tmp = t->cells;
		t->cells = t->alt_cells;
		t->alt_cells = tmp;
		t->alt_screen = false;
	}
	sb_clear(t);
	t->pen = (struct tl_pen){ TL_COLOR_DEFAULT, TL_COLOR_DEFAULT, 0 };
	t->cx = t->cy = 0;
	t->scroll_top = 0;
	t->scroll_bottom = t->rows - 1;
	t->wrap_next = false;
	t->origin_mode = false;
	t->insert_mode = false;
	t->dec_gfx = false;
	t->cursor_style = TL_CURSOR_BLOCK;
	t->cursor_visible = true;
	t->autowrap = true;
	t->app_cursor = false;
	t->app_keypad = false;
	t->mouse_mode = TL_MOUSE_NONE;
	t->mouse_sgr = false;
	t->bracketed_paste = false;
	t->saved = (struct tl_saved){ 0, 0, false, false, false, t->pen };
	t->alt_saved = t->saved;
	fill_rows(t, 0, t->rows);
	if (t->alt_cells != NULL)
		clear_grid(t->alt_cells, t->cols, t->rows);
	mark_all(t);
}

int tl_term_init(struct tl_term *t, int cols, int rows)
{
	memset(t, 0, sizeof(*t));
	if (cols < 1 || rows < 1 || cols > TERM_MAX_DIM || rows > TERM_MAX_DIM)
		return -1;
	size_t n = (size_t)cols * (size_t)rows;
	t->cells = malloc(n * sizeof(*t->cells));
	t->alt_cells = malloc(n * sizeof(*t->alt_cells));
	t->dirty_rows = calloc((size_t)rows, sizeof(*t->dirty_rows));
	if (t->cells == NULL || t->alt_cells == NULL || t->dirty_rows == NULL) {
		tl_term_free(t);
		return -1;
	}
	t->cols = cols;
	t->rows = rows;
	tl_vt_init(&t->vt);
	reset(t);
	return 0;
}

void tl_term_free(struct tl_term *t)
{
	sb_clear(t);
	free(t->cells);
	free(t->alt_cells);
	free(t->dirty_rows);
	t->cells = NULL;
	t->alt_cells = NULL;
	t->dirty_rows = NULL;
}

static struct tl_cell *resize_grid(const struct tl_cell *old, int old_cols,
	int old_rows, int new_cols, int new_rows, int skip)
{
	struct tl_cell *cells = malloc((size_t)new_cols * (size_t)new_rows *
		sizeof(*cells));
	if (cells == NULL)
		return NULL;
	struct tl_cell b = { ' ', { 0, 0 }, TL_COLOR_DEFAULT, TL_COLOR_DEFAULT, 0, 0 };
	for (int y = 0; y < new_rows; y++) {
		for (int x = 0; x < new_cols; x++) {
			int oy = y + skip;
			struct tl_cell c = b;
			if (oy >= 0 && oy < old_rows && x < old_cols)
				c = old[(size_t)oy * (size_t)old_cols + (size_t)x];
			/* A wide character cut at the new right edge. */
			if (x == new_cols - 1 && c.cp != TL_CELL_WIDE_TAIL &&
			    x + 1 < old_cols && oy >= 0 && oy < old_rows &&
			    old[(size_t)oy * (size_t)old_cols + (size_t)(x + 1)].cp ==
				    TL_CELL_WIDE_TAIL) {
				c.cp = ' ';
				c.ncomb = 0;
			}
			cells[(size_t)y * (size_t)new_cols + (size_t)x] = c;
		}
	}
	return cells;
}

int tl_term_resize(struct tl_term *t, int cols, int rows)
{
	if (cols < 1 || rows < 1 || cols > TERM_MAX_DIM || rows > TERM_MAX_DIM)
		return -1;
	if (cols == t->cols && rows == t->rows)
		return 0;
	/* Drop rows from the top when the cursor would fall off. */
	int skip = t->cy - (rows - 1);
	if (skip < 0)
		skip = 0;
	struct tl_cell *cells = resize_grid(t->cells, t->cols, t->rows, cols,
		rows, skip);
	struct tl_cell *alt = resize_grid(t->alt_cells, t->cols, t->rows, cols,
		rows, t->alt_screen ? skip : 0);
	bool *dirty_rows = calloc((size_t)rows, sizeof(*dirty_rows));
	if (cells == NULL || alt == NULL || dirty_rows == NULL) {
		free(cells);
		free(alt);
		free(dirty_rows);
		return -1;
	}
	free(t->cells);
	free(t->alt_cells);
	free(t->dirty_rows);
	t->cells = cells;
	t->alt_cells = alt;
	t->dirty_rows = dirty_rows;
	t->cols = cols;
	t->rows = rows;
	t->cy -= skip;
	if (t->cy < 0)
		t->cy = 0;
	if (t->cx >= cols)
		t->cx = cols - 1;
	t->wrap_next = false;
	t->scroll_top = 0;
	t->scroll_bottom = rows - 1;
	if (t->view_offset > t->sb_len)
		t->view_offset = t->sb_len;
	if (t->saved.cx >= cols)
		t->saved.cx = cols - 1;
	if (t->saved.cy >= rows)
		t->saved.cy = rows - 1;
	if (t->alt_saved.cx >= cols)
		t->alt_saved.cx = cols - 1;
	if (t->alt_saved.cy >= rows)
		t->alt_saved.cy = rows - 1;
	mark_all(t);
	return 0;
}

static void scroll_up(struct tl_term *t, int top, int bot, int n)
{
	if (top < 0)
		top = 0;
	if (bot >= t->rows)
		bot = t->rows - 1;
	int count = bot - top + 1;
	if (n > count)
		n = count;
	if (n <= 0)
		return;
	if (!t->alt_screen && top == 0 && bot == t->rows - 1) {
		for (int i = 0; i < n; i++)
			sb_push_row(t, tl_term_cell(t, 0, i));
	}
	memmove(tl_term_cell(t, 0, top), tl_term_cell(t, 0, top + n),
		(size_t)(count - n) * (size_t)t->cols * sizeof(struct tl_cell));
	fill_rows(t, bot - n + 1, bot + 1);
	if (t->view_offset > 0)
		mark_all(t);
	else
		mark_rows(t, top, bot + 1);
}

static void scroll_down(struct tl_term *t, int top, int bot, int n)
{
	if (top < 0)
		top = 0;
	if (bot >= t->rows)
		bot = t->rows - 1;
	int count = bot - top + 1;
	if (n > count)
		n = count;
	if (n <= 0)
		return;
	memmove(tl_term_cell(t, 0, top + n), tl_term_cell(t, 0, top),
		(size_t)(count - n) * (size_t)t->cols * sizeof(struct tl_cell));
	fill_rows(t, top, top + n);
	mark_rows(t, top, bot + 1);
}

static void linefeed(struct tl_term *t)
{
	if (t->cy == t->scroll_bottom)
		scroll_up(t, t->scroll_top, t->scroll_bottom, 1);
	else if (t->cy < t->rows - 1)
		t->cy++;
	t->wrap_next = false;
	mark_row(t, t->cy);
}

static void move_to(struct tl_term *t, int x, int y)
{
	t->cx = x < 0 ? 0 : x >= t->cols ? t->cols - 1 : x;
	t->cy = y < 0 ? 0 : y >= t->rows ? t->rows - 1 : y;
	t->wrap_next = false;
	mark_row(t, t->cy);
}

static void move_to_origin(struct tl_term *t, int x, int y)
{
	if (t->origin_mode) {
		int min_y = t->scroll_top;
		int max_y = t->scroll_bottom;
		y += min_y;
		if (y < min_y)
			y = min_y;
		if (y > max_y)
			y = max_y;
	}
	move_to(t, x, y);
}

/* Overwriting half of a wide character blanks the other half. */
static void unwide(struct tl_term *t, int x, int y)
{
	struct tl_cell *c = tl_term_cell(t, x, y);
	if (c->cp == TL_CELL_WIDE_TAIL && x > 0) {
		struct tl_cell *head = tl_term_cell(t, x - 1, y);
		head->cp = ' ';
		head->ncomb = 0;
	}
	if (x + 1 < t->cols && tl_term_cell(t, x + 1, y)->cp == TL_CELL_WIDE_TAIL) {
		struct tl_cell *tail = tl_term_cell(t, x + 1, y);
		tail->cp = ' ';
		tail->ncomb = 0;
	}
}

/* Insert or delete n cells at the cursor, within the line. */
static void shift_cells(struct tl_term *t, int n, bool insert)
{
	int x = t->cx, room = t->cols - x;
	if (n > room)
		n = room;
	if (n <= 0)
		return;
	struct tl_cell *row = tl_term_cell(t, 0, t->cy);
	unwide(t, x, t->cy);
	if (insert) {
		memmove(row + x + n, row + x, (size_t)(room - n) * sizeof(*row));
		fill(t, x, t->cy, x + n);
	} else {
		memmove(row + x, row + x + n, (size_t)(room - n) * sizeof(*row));
		fill(t, t->cols - n, t->cy, t->cols);
	}
	t->wrap_next = false;
	mark_row(t, t->cy);
}

static uint32_t dec_gfx_map(uint32_t cp)
{
	static const uint32_t map[32] = {
		' ',    0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0,
		0x00b1, 0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c,
		0x23ba, 0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534,
		0x252c, 0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7,
	};
	return map[cp - 0x5f];
}

void tl_term_print(struct tl_term *t, uint32_t cp)
{
	if (t->dec_gfx && cp >= 0x5f && cp <= 0x7e)
		cp = dec_gfx_map(cp);

	int w = tam_uni_width(cp);
	if (w == 0) {
		if (cp < 0x20)
			return;
		int px = t->wrap_next ? t->cx : (t->cx > 0 ? t->cx - 1 : -1);
		if (px < 0)
			return;
		struct tl_cell *prev = tl_term_cell(t, px, t->cy);
		if (prev->cp == TL_CELL_WIDE_TAIL && px > 0)
			prev = tl_term_cell(t, px - 1, t->cy);
		if (prev->cp != TL_CELL_WIDE_TAIL && prev->ncomb < TL_CELL_COMB_MAX) {
			prev->comb[prev->ncomb++] = cp;
			mark_row(t, t->cy);
		}
		return;
	}
	if (w < 0 || w > t->cols)
		return;
	if (t->wrap_next && t->autowrap) {
		t->cx = 0;
		linefeed(t);
	}
	if (t->cx + w > t->cols) {
		if (!t->autowrap)
			return;
		fill(t, t->cx, t->cy, t->cols);
		t->cx = 0;
		linefeed(t);
	}
	if (t->insert_mode)
		shift_cells(t, w, true);
	unwide(t, t->cx, t->cy);
	struct tl_cell *c = tl_term_cell(t, t->cx, t->cy);
	*c = (struct tl_cell){ cp, { 0, 0 }, t->pen.fg, t->pen.bg, t->pen.attrs, 0 };
	if (w == 2) {
		unwide(t, t->cx + 1, t->cy);
		c[1] = *c;
		c[1].cp = TL_CELL_WIDE_TAIL;
	}
	mark_row(t, t->cy);
	if (t->cx + w >= t->cols) {
		t->cx = t->cols - 1;
		t->wrap_next = t->autowrap;
	} else {
		t->cx += w;
		t->wrap_next = false;
	}
}

void tl_term_control(struct tl_term *t, unsigned char c)
{
	switch (c) {
	case '\b':
		if (t->cx > 0)
			t->cx--;
		t->wrap_next = false;
		break;
	case '\t': {
		int x = (t->cx / 8 + 1) * 8;
		t->cx = x >= t->cols ? t->cols - 1 : x;
		t->wrap_next = false;
		break;
	}
	case '\n':
	case '\v':
	case '\f':
		linefeed(t);
		break;
	case '\r':
		t->cx = 0;
		t->wrap_next = false;
		break;
	default:
		/* BEL, SO, SI and the rest: nothing yet. */
		return;
	}
	mark_row(t, t->cy);
}

static void reply(struct tl_term *t, const char *s)
{
	size_t n = strlen(s);
	if (t->reply_len + n <= sizeof(t->reply)) {
		memcpy(t->reply + t->reply_len, s, n);
		t->reply_len += n;
	}
}

static void save_cursor(struct tl_term *t)
{
	t->saved = (struct tl_saved){
		t->cx, t->cy, t->wrap_next, t->origin_mode, t->dec_gfx, t->pen
	};
}

static void restore_cursor(struct tl_term *t)
{
	t->origin_mode = t->saved.origin;
	t->dec_gfx = t->saved.dec_gfx;
	move_to(t, t->saved.cx, t->saved.cy);
	t->wrap_next = t->saved.wrap_next;
	t->pen = t->saved.pen;
}

void tl_term_esc(struct tl_term *t, char inter, char final)
{
	if (inter == '(') {
		if (final == '0')
			t->dec_gfx = true;
		else if (final == 'B')
			t->dec_gfx = false;
		return;
	}
	if (inter != 0)
		return;
	switch (final) {
	case '7':
		save_cursor(t);
		break;
	case '8':
		restore_cursor(t);
		break;
	case 'D':
		linefeed(t);
		break;
	case 'E':
		t->cx = 0;
		linefeed(t);
		break;
	case 'M':
		if (t->cy == t->scroll_top)
			scroll_down(t, t->scroll_top, t->scroll_bottom, 1);
		else if (t->cy > 0)
			t->cy--;
		t->wrap_next = false;
		mark_row(t, t->cy);
		break;
	case 'c':
		reset(t);
		break;
	case '=':
		t->app_keypad = true;
		break;
	case '>':
		t->app_keypad = false;
		break;
	default:
		return;
	}
	mark_row(t, t->cy);
}

static int arg(const struct tl_csi *c, int i, int dflt)
{
	if (i >= c->n || c->p[i] == 0)
		return dflt;
	return c->p[i];
}

/* 38 and 48: 5;n or 2;r;g;b, with ';' or ':' between. Returns how many
 * parameters after the 38 or 48 were used. */
static int extended_color(const struct tl_csi *c, int i, uint32_t *out)
{
	if (i + 1 >= c->n)
		return c->n - i - 1;
	int kind = c->p[i + 1];
	if (kind == 5 && i + 2 < c->n) {
		*out = TL_COLOR_INDEX(c->p[i + 2] & 0xff);
		return 2;
	}
	if (kind == 2) {
		/* 38:2:<colorspace>:r:g:b has one more field than 38;2;r;g;b. */
		int j = i + 2;
		if (c->sub[i + 1] && i + 5 < c->n && c->sub[i + 5])
			j++;
		if (j + 2 < c->n) {
			uint32_t r = (uint32_t)c->p[j] & 0xff;
			uint32_t g = (uint32_t)c->p[j + 1] & 0xff;
			uint32_t b = (uint32_t)c->p[j + 2] & 0xff;
			*out = TL_COLOR_RGB(r << 16 | g << 8 | b);
			return j + 2 - i;
		}
	}
	return c->n - i - 1;
}

static void sgr(struct tl_term *t, const struct tl_csi *c)
{
	struct tl_pen *p = &t->pen;
	if (c->n == 0) {
		*p = (struct tl_pen){ 0, 0, 0 };
		return;
	}
	for (int i = 0; i < c->n; i++) {
		int v = c->p[i];
		if (i > 0 && c->sub[i])
			continue; /* sub-parameters we do not use (4:3 curly) */
		switch (v) {
		case 0: *p = (struct tl_pen){ 0, 0, 0 }; break;
		case 1: p->attrs |= TL_ATTR_BOLD; break;
		case 2: p->attrs |= TL_ATTR_DIM; break;
		case 3: p->attrs |= TL_ATTR_ITALIC; break;
		case 4: p->attrs |= TL_ATTR_UNDERLINE; break;
		case 5: p->attrs |= TL_ATTR_BLINK; break;
		case 7: p->attrs |= TL_ATTR_INVERSE; break;
		case 8: p->attrs |= TL_ATTR_HIDDEN; break;
		case 9: p->attrs |= TL_ATTR_STRIKE; break;
		case 21: p->attrs |= TL_ATTR_UNDERLINE; break;
		case 22: p->attrs &= ~(TL_ATTR_BOLD | TL_ATTR_DIM); break;
		case 23: p->attrs &= ~TL_ATTR_ITALIC; break;
		case 24: p->attrs &= ~TL_ATTR_UNDERLINE; break;
		case 25: p->attrs &= ~TL_ATTR_BLINK; break;
		case 27: p->attrs &= ~TL_ATTR_INVERSE; break;
		case 28: p->attrs &= ~TL_ATTR_HIDDEN; break;
		case 29: p->attrs &= ~TL_ATTR_STRIKE; break;
		case 38: i += extended_color(c, i, &p->fg); break;
		case 39: p->fg = TL_COLOR_DEFAULT; break;
		case 48: i += extended_color(c, i, &p->bg); break;
		case 49: p->bg = TL_COLOR_DEFAULT; break;
		default:
			if (v >= 30 && v <= 37)
				p->fg = TL_COLOR_INDEX(v - 30);
			else if (v >= 40 && v <= 47)
				p->bg = TL_COLOR_INDEX(v - 40);
			else if (v >= 90 && v <= 97)
				p->fg = TL_COLOR_INDEX(v - 90 + 8);
			else if (v >= 100 && v <= 107)
				p->bg = TL_COLOR_INDEX(v - 100 + 8);
			break;
		}
	}
}

static void switch_screen(struct tl_term *t, bool alt, bool clear_alt,
	bool save_on_enter)
{
	if (alt == t->alt_screen) {
		if (alt && clear_alt) {
			fill_rows(t, 0, t->rows);
			mark_all(t);
		}
		return;
	}
	if (alt && save_on_enter)
		save_cursor(t);

	struct tl_cell *tmp_cells = t->cells;
	t->cells = t->alt_cells;
	t->alt_cells = tmp_cells;

	struct tl_saved tmp_saved = t->saved;
	t->saved = t->alt_saved;
	t->alt_saved = tmp_saved;

	t->alt_screen = alt;
	t->view_offset = 0;
	t->scroll_top = 0;
	t->scroll_bottom = t->rows - 1;
	if (alt && clear_alt)
		fill_rows(t, 0, t->rows);
	if (!alt && save_on_enter)
		restore_cursor(t);
	mark_all(t);
}

static void dec_mode(struct tl_term *t, const struct tl_csi *c, bool on)
{
	for (int i = 0; i < c->n; i++) {
		switch (c->p[i]) {
		case 1: t->app_cursor = on; break;
		case 6:
			t->origin_mode = on;
			move_to_origin(t, 0, 0);
			break;
		case 7: t->autowrap = on; if (!on) t->wrap_next = false; break;
		case 25:
			t->cursor_visible = on;
			mark_row(t, t->cy);
			break;
		case 47:
			switch_screen(t, on, false, false);
			break;
		case 1047:
			switch_screen(t, on, true, false);
			break;
		case 1049:
			switch_screen(t, on, true, true);
			break;
		case 1000:
		case 1002:
		case 1003:
			if (on)
				t->mouse_mode = (enum tl_mouse_mode)c->p[i];
			else if ((int)t->mouse_mode == c->p[i])
				t->mouse_mode = TL_MOUSE_NONE;
			break;
		case 1006:
			t->mouse_sgr = on;
			break;
		case 2004:
			t->bracketed_paste = on;
			break;
		default: break;
		}
	}
}

static void ansi_mode(struct tl_term *t, const struct tl_csi *c, bool on)
{
	for (int i = 0; i < c->n; i++) {
		if (c->p[i] == 4)
			t->insert_mode = on;
	}
}

void tl_term_csi(struct tl_term *t, const struct tl_csi *c)
{
	char buf[48];

	if (c->priv == '?') {
		if (c->inter == 0 && c->final == 'h')
			dec_mode(t, c, true);
		else if (c->inter == 0 && c->final == 'l')
			dec_mode(t, c, false);
		return;
	}
	if (c->priv != 0)
		return;
	if (c->inter == ' ') {
		if (c->final == 'q') {
			switch (arg(c, 0, 0)) {
			case 0: case 1: case 2:
				t->cursor_style = TL_CURSOR_BLOCK;
				break;
			case 3: case 4:
				t->cursor_style = TL_CURSOR_UNDERLINE;
				break;
			case 5: case 6:
				t->cursor_style = TL_CURSOR_BAR;
				break;
			}
			mark_row(t, t->cy);
		}
		return;
	}
	if (c->inter != 0)
		return;

	int n = arg(c, 0, 1);
	switch (c->final) {
	case 'A': {
		int top = t->cy >= t->scroll_top ? t->scroll_top : 0;
		int ny = t->cy - n;
		move_to(t, t->cx, ny < top ? top : ny);
		break;
	}
	case 'B': {
		int bot = t->cy <= t->scroll_bottom ? t->scroll_bottom : t->rows - 1;
		int ny = t->cy + n;
		move_to(t, t->cx, ny > bot ? bot : ny);
		break;
	}
	case 'C': move_to(t, t->cx + n, t->cy); break;
	case 'D': move_to(t, t->cx - n, t->cy); break;
	case 'E': move_to(t, 0, t->cy + n); break;
	case 'F': move_to(t, 0, t->cy - n); break;
	case 'G': case '`': move_to(t, n - 1, t->cy); break;
	case 'd': move_to_origin(t, t->cx, n - 1); break;
	case 'H': case 'f': move_to_origin(t, arg(c, 1, 1) - 1, n - 1); break;
	case 'J':
		switch (arg(c, 0, 0)) {
		case 0:
			fill(t, t->cx, t->cy, t->cols);
			fill_rows(t, t->cy + 1, t->rows);
			break;
		case 1:
			fill_rows(t, 0, t->cy);
			fill(t, 0, t->cy, t->cx + 1);
			break;
		case 2:
			fill_rows(t, 0, t->rows);
			mark_all(t);
			break;
		case 3:
			if (!t->alt_screen)
				sb_clear(t);
			fill_rows(t, 0, t->rows);
			mark_all(t);
			break;
		}
		break;
	case 'K':
		switch (arg(c, 0, 0)) {
		case 0: fill(t, t->cx, t->cy, t->cols); break;
		case 1: fill(t, 0, t->cy, t->cx + 1); break;
		case 2: fill(t, 0, t->cy, t->cols); break;
		}
		break;
	case '@': shift_cells(t, n, true); break;
	case 'P': shift_cells(t, n, false); break;
	case 'X':
		unwide(t, t->cx, t->cy);
		fill(t, t->cx, t->cy, t->cx + n);
		break;
	case 'L':
		if (t->cy >= t->scroll_top && t->cy <= t->scroll_bottom) {
			scroll_down(t, t->cy, t->scroll_bottom, n);
			move_to(t, 0, t->cy);
		}
		break;
	case 'M':
		if (t->cy >= t->scroll_top && t->cy <= t->scroll_bottom) {
			scroll_up(t, t->cy, t->scroll_bottom, n);
			move_to(t, 0, t->cy);
		}
		break;
	case 'S': scroll_up(t, t->scroll_top, t->scroll_bottom, n); break;
	case 'T': scroll_down(t, t->scroll_top, t->scroll_bottom, n); break;
	case 'h': ansi_mode(t, c, true); break;
	case 'l': ansi_mode(t, c, false); break;
	case 'r': {
		int top = arg(c, 0, 1) - 1;
		int bot = arg(c, 1, t->rows) - 1;
		if (top < 0)
			top = 0;
		if (bot >= t->rows)
			bot = t->rows - 1;
		if (top < bot) {
			t->scroll_top = top;
			t->scroll_bottom = bot;
			move_to_origin(t, 0, 0);
		}
		break;
	}
	case 'm': sgr(t, c); break;
	case 'n':
		if (arg(c, 0, 0) == 5) {
			reply(t, "\033[0n");
		} else if (arg(c, 0, 0) == 6) {
			int ry = t->origin_mode ? t->cy - t->scroll_top + 1 : t->cy + 1;
			snprintf(buf, sizeof(buf), "\033[%d;%dR", ry, t->cx + 1);
			reply(t, buf);
		}
		break;
	case 'c':
		if (arg(c, 0, 0) == 0)
			reply(t, "\033[?62;22c"); /* VT220 with ANSI color */
		break;
	default:
		break;
	}
}

void tl_term_feed(struct tl_term *t, const void *data, size_t len)
{
	tl_vt_feed(&t->vt, t, data, len);
}

void tl_term_scroll_view(struct tl_term *t, int delta)
{
	if (t->alt_screen || t->sb_len == 0) {
		t->view_offset = 0;
		return;
	}
	int next = t->view_offset + delta;
	if (next < 0)
		next = 0;
	if (next > t->sb_len)
		next = t->sb_len;
	if (next != t->view_offset) {
		t->view_offset = next;
		mark_all(t);
	}
}

void tl_term_reset_view(struct tl_term *t)
{
	if (t->view_offset != 0) {
		t->view_offset = 0;
		mark_all(t);
	}
}

const struct tl_cell *tl_term_abs_row(const struct tl_term *t, int abs_y,
	int *out_cols)
{
	int sb = t->alt_screen ? 0 : t->sb_len;
	if (abs_y < 0 || abs_y >= sb + t->rows) {
		*out_cols = 0;
		return NULL;
	}
	if (abs_y < sb) {
		int rem = abs_y;
		for (int i = 0; i < t->sb_count; i++) {
			int idx = (t->sb_head + i) % TL_SB_RING_CAP;
			const struct tl_sb_page *p = &t->sb_pages[idx];
			if (rem < p->used) {
				*out_cols = p->cols;
				return p->cells + (size_t)rem * (size_t)p->cols;
			}
			rem -= p->used;
		}
		*out_cols = 0;
		return NULL;
	}
	*out_cols = t->cols;
	return tl_term_cell(t, 0, abs_y - sb);
}

const struct tl_cell *tl_term_view_row(const struct tl_term *t, int y,
	int *out_cols)
{
	if (t->alt_screen || t->view_offset == 0) {
		if (y < 0 || y >= t->rows) {
			*out_cols = 0;
			return NULL;
		}
		*out_cols = t->cols;
		return tl_term_cell(t, 0, y);
	}
	int abs_y = t->sb_len - t->view_offset + y;
	return tl_term_abs_row(t, abs_y, out_cols);
}
