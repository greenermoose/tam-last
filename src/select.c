/* Text selection: normalized range, word/line snapping, UTF-8 extraction. */
#include "tam-last/select.h"

#include <stdlib.h>
#include <string.h>

static bool is_word_cp(uint32_t cp)
{
	if (cp == ' ' || cp == '\t' || cp == 0 || cp == TL_CELL_WIDE_TAIL)
		return false;
	if (cp >= 0x80)
		return true;
	if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') ||
	    (cp >= 'a' && cp <= 'z') || cp == '_' || cp == '-' || cp == '.' ||
	    cp == '/' || cp == '~' || cp == ':')
		return true;
	return false;
}

static uint32_t row_cp(const struct tl_cell *row, int ncols, int x)
{
	if (row == NULL || x < 0 || x >= ncols)
		return ' ';
	uint32_t cp = row[x].cp;
	if (cp == TL_CELL_WIDE_TAIL && x > 0)
		return row[x - 1].cp;
	return cp;
}

static void word_bounds(const struct tl_term *t, int x, int abs_y, int *l, int *r)
{
	int ncols = 0;
	const struct tl_cell *row = tl_term_abs_row(t, abs_y, &ncols);
	if (row == NULL || ncols <= 0) {
		*l = *r = x;
		return;
	}
	if (x < 0)
		x = 0;
	if (x >= ncols)
		x = ncols - 1;
	bool w = is_word_cp(row_cp(row, ncols, x));
	int lx = x, rx = x;
	while (lx > 0 && is_word_cp(row_cp(row, ncols, lx - 1)) == w)
		lx--;
	while (rx + 1 < ncols && is_word_cp(row_cp(row, ncols, rx + 1)) == w)
		rx++;
	*l = lx;
	*r = rx;
}

static void normalize(struct tl_sel *s, const struct tl_term *t)
{
	int x0 = s->ax, y0 = s->ay;
	int x1 = s->bx, y1 = s->by;
	if (y0 > y1 || (y0 == y1 && x0 > x1)) {
		int tx = x0, ty = y0;
		x0 = x1; y0 = y1;
		x1 = tx; y1 = ty;
	}
	if (s->kind == TL_SEL_WORD) {
		int l0, r0, l1, r1;
		word_bounds(t, x0, y0, &l0, &r0);
		word_bounds(t, x1, y1, &l1, &r1);
		x0 = l0;
		x1 = r1;
	} else if (s->kind == TL_SEL_LINE) {
		x0 = 0;
		x1 = t->cols - 1;
	}
	s->sx = x0;
	s->sy = y0;
	s->ex = x1;
	s->ey = y1;
}

void tl_sel_start(struct tl_sel *s, const struct tl_term *t, int x, int abs_y,
	enum tl_sel_kind kind)
{
	s->active = true;
	s->dragging = true;
	s->kind = kind;
	s->ax = s->bx = x;
	s->ay = s->by = abs_y;
	normalize(s, t);
}

void tl_sel_update(struct tl_sel *s, const struct tl_term *t, int x, int abs_y)
{
	if (!s->active)
		return;
	s->bx = x;
	s->by = abs_y;
	normalize(s, t);
}

void tl_sel_end(struct tl_sel *s)
{
	s->dragging = false;
}

void tl_sel_clear(struct tl_sel *s)
{
	memset(s, 0, sizeof(*s));
}

bool tl_sel_contains(const struct tl_sel *s, int x, int abs_y)
{
	if (!s->active)
		return false;
	if (abs_y < s->sy || abs_y > s->ey)
		return false;
	if (s->sy == s->ey)
		return x >= s->sx && x <= s->ex;
	if (abs_y == s->sy)
		return x >= s->sx;
	if (abs_y == s->ey)
		return x <= s->ex;
	return true;
}

static size_t utf8_encode(uint32_t cp, char out[4])
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	} else if (cp < 0x800) {
		out[0] = (char)(0xc0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3f));
		return 2;
	} else if (cp < 0x10000) {
		out[0] = (char)(0xe0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[2] = (char)(0x80 | (cp & 0x3f));
		return 3;
	} else if (cp <= 0x10ffff) {
		out[0] = (char)(0xf0 | (cp >> 18));
		out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
		out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[3] = (char)(0x80 | (cp & 0x3f));
		return 4;
	}
	return 0;
}

static int buf_append(char **buf, size_t *len, size_t *cap, const char *s,
	size_t n)
{
	if (*len + n + 1 > *cap) {
		size_t ncap = *cap == 0 ? 256 : *cap * 2;
		while (ncap < *len + n + 1)
			ncap *= 2;
		char *nb = realloc(*buf, ncap);
		if (nb == NULL)
			return -1;
		*buf = nb;
		*cap = ncap;
	}
	memcpy(*buf + *len, s, n);
	*len += n;
	(*buf)[*len] = '\0';
	return 0;
}

char *tl_sel_extract(const struct tl_term *t, const struct tl_sel *s,
	size_t *out_len)
{
	if (out_len != NULL)
		*out_len = 0;
	if (!s->active)
		return NULL;
	char *buf = NULL;
	size_t len = 0, cap = 0;

	for (int y = s->sy; y <= s->ey; y++) {
		int ncols = 0;
		const struct tl_cell *row = tl_term_abs_row(t, y, &ncols);
		int x0 = (y == s->sy) ? s->sx : 0;
		int x1 = (y == s->ey) ? s->ex : (ncols - 1);
		if (x0 < 0)
			x0 = 0;
		if (x1 >= ncols)
			x1 = ncols - 1;
		/* Trim trailing spaces on each line. */
		int last = x0 - 1;
		if (row != NULL) {
			for (int x = x1; x >= x0; x--) {
				if (row[x].cp != ' ' || row[x].ncomb > 0) {
					last = x;
					break;
				}
			}
		}
		for (int x = x0; x <= last; x++) {
			const struct tl_cell *c = &row[x];
			if (c->cp == TL_CELL_WIDE_TAIL)
				continue;
			char u8[4];
			size_t un = utf8_encode(c->cp, u8);
			if (buf_append(&buf, &len, &cap, u8, un) != 0)
				goto oom;
			for (uint8_t i = 0; i < c->ncomb; i++) {
				un = utf8_encode(c->comb[i], u8);
				if (buf_append(&buf, &len, &cap, u8, un) != 0)
					goto oom;
			}
		}
		if (y < s->ey) {
			if (buf_append(&buf, &len, &cap, "\n", 1) != 0)
				goto oom;
		}
	}
	if (buf == NULL) {
		buf = malloc(1);
		if (buf == NULL)
			return NULL;
		buf[0] = '\0';
	}
	if (out_len != NULL)
		*out_len = len;
	return buf;

oom:
	free(buf);
	return NULL;
}
