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
	return (struct tl_cell){ ' ', TL_COLOR_DEFAULT, t->pen.bg, 0 };
}

static void fill(struct tl_term *t, int x0, int y, int x1)
{
	struct tl_cell b = blank(t);
	if (x0 < 0)
		x0 = 0;
	if (x1 > t->cols)
		x1 = t->cols;
	for (int x = x0; x < x1; x++)
		*tl_term_cell(t, x, y) = b;
	t->dirty = true;
}

static void fill_rows(struct tl_term *t, int y0, int y1)
{
	for (int y = y0; y < y1; y++)
		fill(t, 0, y, t->cols);
}

static void reset(struct tl_term *t)
{
	t->pen = (struct tl_pen){ TL_COLOR_DEFAULT, TL_COLOR_DEFAULT, 0 };
	t->cx = t->cy = 0;
	t->wrap_next = false;
	t->cursor_visible = true;
	t->autowrap = true;
	t->app_cursor = false;
	t->app_keypad = false;
	t->saved.cx = t->saved.cy = 0;
	t->saved.wrap_next = false;
	t->saved.pen = t->pen;
	fill_rows(t, 0, t->rows);
}

int tl_term_init(struct tl_term *t, int cols, int rows)
{
	memset(t, 0, sizeof(*t));
	if (cols < 1 || rows < 1 || cols > TERM_MAX_DIM || rows > TERM_MAX_DIM)
		return -1;
	t->cells = malloc((size_t)cols * (size_t)rows * sizeof(*t->cells));
	if (t->cells == NULL)
		return -1;
	t->cols = cols;
	t->rows = rows;
	tl_vt_init(&t->vt);
	reset(t);
	return 0;
}

void tl_term_free(struct tl_term *t)
{
	free(t->cells);
	t->cells = NULL;
}

int tl_term_resize(struct tl_term *t, int cols, int rows)
{
	if (cols < 1 || rows < 1 || cols > TERM_MAX_DIM || rows > TERM_MAX_DIM)
		return -1;
	if (cols == t->cols && rows == t->rows)
		return 0;
	struct tl_cell *cells = malloc((size_t)cols * (size_t)rows *
		sizeof(*cells));
	if (cells == NULL)
		return -1;
	/* Drop rows from the top when the cursor would fall off. */
	int skip = t->cy - (rows - 1);
	if (skip < 0)
		skip = 0;
	struct tl_cell b = { ' ', TL_COLOR_DEFAULT, TL_COLOR_DEFAULT, 0 };
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			int oy = y + skip;
			struct tl_cell c = b;
			if (oy < t->rows && x < t->cols)
				c = *tl_term_cell(t, x, oy);
			/* A wide character cut at the new right edge. */
			if (x == cols - 1 && c.cp != TL_CELL_WIDE_TAIL &&
			    x + 1 < t->cols && oy < t->rows &&
			    tl_term_cell(t, x + 1, oy)->cp == TL_CELL_WIDE_TAIL)
				c.cp = ' ';
			cells[(size_t)y * (size_t)cols + (size_t)x] = c;
		}
	}
	free(t->cells);
	t->cells = cells;
	t->cols = cols;
	t->rows = rows;
	t->cy -= skip;
	if (t->cx >= cols)
		t->cx = cols - 1;
	t->wrap_next = false;
	if (t->saved.cx >= cols)
		t->saved.cx = cols - 1;
	if (t->saved.cy >= rows)
		t->saved.cy = rows - 1;
	t->dirty = true;
	return 0;
}

static void scroll_up(struct tl_term *t, int top, int n)
{
	int count = t->rows - top;
	if (n > count)
		n = count;
	if (n <= 0)
		return;
	memmove(tl_term_cell(t, 0, top), tl_term_cell(t, 0, top + n),
		(size_t)(count - n) * (size_t)t->cols * sizeof(struct tl_cell));
	fill_rows(t, t->rows - n, t->rows);
}

static void scroll_down(struct tl_term *t, int top, int n)
{
	int count = t->rows - top;
	if (n > count)
		n = count;
	if (n <= 0)
		return;
	memmove(tl_term_cell(t, 0, top + n), tl_term_cell(t, 0, top),
		(size_t)(count - n) * (size_t)t->cols * sizeof(struct tl_cell));
	fill_rows(t, top, top + n);
}

static void linefeed(struct tl_term *t)
{
	if (t->cy == t->rows - 1)
		scroll_up(t, 0, 1);
	else
		t->cy++;
	t->wrap_next = false;
}

static void move_to(struct tl_term *t, int x, int y)
{
	t->cx = x < 0 ? 0 : x >= t->cols ? t->cols - 1 : x;
	t->cy = y < 0 ? 0 : y >= t->rows ? t->rows - 1 : y;
	t->wrap_next = false;
	t->dirty = true;
}

/* Overwriting half of a wide character blanks the other half. */
static void unwide(struct tl_term *t, int x, int y)
{
	struct tl_cell *c = tl_term_cell(t, x, y);
	if (c->cp == TL_CELL_WIDE_TAIL && x > 0)
		tl_term_cell(t, x - 1, y)->cp = ' ';
	if (x + 1 < t->cols && tl_term_cell(t, x + 1, y)->cp == TL_CELL_WIDE_TAIL)
		tl_term_cell(t, x + 1, y)->cp = ' ';
}

void tl_term_print(struct tl_term *t, uint32_t cp)
{
	int w = tam_uni_width(cp);
	if (w <= 0)
		return; /* combining marks arrive in 0.0.2 */
	if (w > t->cols)
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
	unwide(t, t->cx, t->cy);
	struct tl_cell *c = tl_term_cell(t, t->cx, t->cy);
	*c = (struct tl_cell){ cp, t->pen.fg, t->pen.bg, t->pen.attrs };
	if (w == 2) {
		unwide(t, t->cx + 1, t->cy);
		c[1] = *c;
		c[1].cp = TL_CELL_WIDE_TAIL;
	}
	t->dirty = true;
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
	t->dirty = true;
}

static void reply(struct tl_term *t, const char *s)
{
	size_t n = strlen(s);
	if (t->reply_len + n <= sizeof(t->reply)) {
		memcpy(t->reply + t->reply_len, s, n);
		t->reply_len += n;
	}
}

void tl_term_esc(struct tl_term *t, char inter, char final)
{
	if (inter != 0)
		return; /* character sets: 0.0.2 */
	switch (final) {
	case '7':
		t->saved.cx = t->cx;
		t->saved.cy = t->cy;
		t->saved.wrap_next = t->wrap_next;
		t->saved.pen = t->pen;
		break;
	case '8':
		move_to(t, t->saved.cx, t->saved.cy);
		t->wrap_next = t->saved.wrap_next;
		t->pen = t->saved.pen;
		break;
	case 'D':
		linefeed(t);
		break;
	case 'E':
		t->cx = 0;
		linefeed(t);
		break;
	case 'M':
		if (t->cy == 0)
			scroll_down(t, 0, 1);
		else
			t->cy--;
		t->wrap_next = false;
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
	t->dirty = true;
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

static void dec_mode(struct tl_term *t, const struct tl_csi *c, bool on)
{
	for (int i = 0; i < c->n; i++) {
		switch (c->p[i]) {
		case 1: t->app_cursor = on; break;
		case 7: t->autowrap = on; if (!on) t->wrap_next = false; break;
		case 25: t->cursor_visible = on; break;
		default: break; /* 1049 and friends: 0.0.2 */
		}
	}
	t->dirty = true;
}

/* Insert or delete n cells at the cursor, within the line. */
static void shift_cells(struct tl_term *t, int n, bool insert)
{
	int x = t->cx, room = t->cols - x;
	if (n > room)
		n = room;
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
	if (c->priv != 0 || c->inter != 0)
		return;

	int n = arg(c, 0, 1);
	switch (c->final) {
	case 'A': move_to(t, t->cx, t->cy - n); break;
	case 'B': move_to(t, t->cx, t->cy + n); break;
	case 'C': move_to(t, t->cx + n, t->cy); break;
	case 'D': move_to(t, t->cx - n, t->cy); break;
	case 'E': move_to(t, 0, t->cy + n); break;
	case 'F': move_to(t, 0, t->cy - n); break;
	case 'G': case '`': move_to(t, n - 1, t->cy); break;
	case 'd': move_to(t, t->cx, n - 1); break;
	case 'H': case 'f': move_to(t, arg(c, 1, 1) - 1, n - 1); break;
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
		case 2: case 3:
			fill_rows(t, 0, t->rows);
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
		scroll_down(t, t->cy, n);
		move_to(t, 0, t->cy);
		break;
	case 'M':
		scroll_up(t, t->cy, n);
		move_to(t, 0, t->cy);
		break;
	case 'S': scroll_up(t, 0, n); break;
	case 'T': scroll_down(t, 0, n); break;
	case 'm': sgr(t, c); break;
	case 'n':
		if (arg(c, 0, 0) == 5) {
			reply(t, "\033[0n");
		} else if (arg(c, 0, 0) == 6) {
			snprintf(buf, sizeof(buf), "\033[%d;%dR", t->cy + 1,
				t->cx + 1);
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
