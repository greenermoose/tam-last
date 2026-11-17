/* TrueType table parsing, written from Apple's TrueType Reference Manual
 * and Microsoft's OpenType specification. stb_truetype.h was read to
 * check the composite-glyph details; none of its code is used. */
#include "tam-last/ttf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Limits that keep a hostile font from costing much. */
#define TTF_MAX_POINTS 20000
#define TTF_MAX_DEPTH 8

static uint16_t u16(const struct tl_ttf *f, size_t off)
{
	if (off + 2 > f->len)
		return 0;
	return (uint16_t)(f->data[off] << 8 | f->data[off + 1]);
}

static int16_t s16(const struct tl_ttf *f, size_t off)
{
	return (int16_t)u16(f, off);
}

static uint32_t u32(const struct tl_ttf *f, size_t off)
{
	if (off + 4 > f->len)
		return 0;
	return (uint32_t)f->data[off] << 24 | (uint32_t)f->data[off + 1] << 16 |
		(uint32_t)f->data[off + 2] << 8 | f->data[off + 3];
}

static int in_file(const struct tl_ttf *f, size_t off, size_t n)
{
	return off <= f->len && n <= f->len - off;
}

static int find_table(const struct tl_ttf *f, const char tag[4],
	uint32_t *off, uint32_t *len)
{
	uint16_t n = u16(f, 4);
	for (uint16_t i = 0; i < n; i++) {
		size_t rec = 12 + (size_t)i * 16;
		if (!in_file(f, rec, 16))
			return -1;
		if (memcmp(f->data + rec, tag, 4) == 0) {
			*off = u32(f, rec + 8);
			*len = u32(f, rec + 12);
			return in_file(f, *off, *len) ? 0 : -1;
		}
	}
	return -1;
}

/* Prefer a full-Unicode subtable (format 12), then the BMP (format 4). */
static int pick_cmap(struct tl_ttf *f, uint32_t cmap, uint32_t cmap_len)
{
	uint16_t n = u16(f, cmap + 2);
	uint32_t best = 0;
	int best_rank = 0;

	for (uint16_t i = 0; i < n; i++) {
		size_t rec = cmap + 4 + (size_t)i * 8;
		if (rec + 8 > (size_t)cmap + cmap_len)
			break;
		uint16_t platform = u16(f, rec), encoding = u16(f, rec + 2);
		uint32_t sub = cmap + u32(f, rec + 4);
		if (!in_file(f, sub, 8))
			continue;
		uint16_t fmt = u16(f, sub);
		int unicode = platform == 0 ||
			(platform == 3 && (encoding == 1 || encoding == 10));
		int rank = !unicode ? 0 : fmt == 12 ? 2 : fmt == 4 ? 1 : 0;
		if (rank > best_rank) {
			best_rank = rank;
			best = sub;
		}
	}
	if (best_rank == 0)
		return -1;
	f->cmap = best;
	f->cmap_fmt = u16(f, best);
	return 0;
}

int tl_ttf_load(struct tl_ttf *f, const uint8_t *data, size_t len,
	char *err, size_t errlen)
{
	uint32_t off, tlen, cmap, cmap_len;

	memset(f, 0, sizeof(*f));
	f->data = data;
	f->len = len;
	uint32_t version = u32(f, 0);
	if (len < 12 || (version != 0x00010000 && version != 0x74727565)) {
		snprintf(err, errlen, "not a TrueType font%s",
			version == 0x4f54544f ? " (CFF outlines are not "
			"supported yet)" : "");
		return -1;
	}

	if (find_table(f, "head", &off, &tlen) != 0 || tlen < 54) {
		snprintf(err, errlen, "no head table");
		return -1;
	}
	f->units_per_em = u16(f, off + 18);
	f->loca_long = s16(f, off + 50) != 0;
	if (f->units_per_em < 16 || f->units_per_em > 16384) {
		snprintf(err, errlen, "bad unitsPerEm");
		return -1;
	}

	if (find_table(f, "maxp", &off, &tlen) != 0 || tlen < 6) {
		snprintf(err, errlen, "no maxp table");
		return -1;
	}
	f->num_glyphs = u16(f, off + 4);

	if (find_table(f, "hhea", &off, &tlen) != 0 || tlen < 36) {
		snprintf(err, errlen, "no hhea table");
		return -1;
	}
	f->ascender = s16(f, off + 4);
	f->descender = s16(f, off + 6);
	f->line_gap = s16(f, off + 8);
	f->num_hmetrics = u16(f, off + 34);

	if (find_table(f, "hmtx", &f->hmtx, &f->hmtx_len) != 0 ||
	    f->num_hmetrics == 0 ||
	    (size_t)f->num_hmetrics * 4 > f->hmtx_len) {
		snprintf(err, errlen, "no usable hmtx table");
		return -1;
	}
	if (find_table(f, "loca", &f->loca, &f->loca_len) != 0 ||
	    find_table(f, "glyf", &f->glyf, &f->glyf_len) != 0) {
		snprintf(err, errlen, "no glyf outlines");
		return -1;
	}
	if ((size_t)f->loca_len < ((size_t)f->num_glyphs + 1) *
	    (f->loca_long ? 4 : 2)) {
		snprintf(err, errlen, "loca table too short");
		return -1;
	}
	if (find_table(f, "cmap", &cmap, &cmap_len) != 0 ||
	    pick_cmap(f, cmap, cmap_len) != 0) {
		snprintf(err, errlen, "no Unicode character map");
		return -1;
	}
	/* OS/2 version 2 and later give the x-height used for grid fitting. */
	if (find_table(f, "OS/2", &off, &tlen) == 0 && tlen >= 90 &&
	    u16(f, off) >= 2) {
		f->x_height = s16(f, off + 86);
		f->cap_height = s16(f, off + 88);
	}
	return 0;
}

static uint32_t cmap4(const struct tl_ttf *f, uint32_t cp)
{
	size_t t = f->cmap;
	if (cp > 0xffff)
		return 0;
	uint16_t segx2 = u16(f, t + 6);
	size_t ends = t + 14, starts = ends + segx2 + 2;
	size_t deltas = starts + segx2, ranges = deltas + segx2;
	if (!in_file(f, ranges, segx2))
		return 0;

	/* Binary search for the first segment whose end >= cp. */
	size_t lo = 0, hi = segx2 / 2;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		if (u16(f, ends + mid * 2) < cp)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo >= (size_t)segx2 / 2)
		return 0;
	uint16_t start = u16(f, starts + lo * 2);
	if (cp < start)
		return 0;
	uint16_t delta = u16(f, deltas + lo * 2);
	uint16_t range = u16(f, ranges + lo * 2);
	if (range == 0)
		return (uint16_t)(cp + delta);
	size_t at = ranges + lo * 2 + range + (cp - start) * 2;
	uint16_t g = u16(f, at);
	return g == 0 ? 0 : (uint16_t)(g + delta);
}

static uint32_t cmap12(const struct tl_ttf *f, uint32_t cp)
{
	size_t t = f->cmap;
	uint32_t n = u32(f, t + 12);
	if (!in_file(f, t + 16, (size_t)n * 12))
		return 0;
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		size_t g = t + 16 + mid * 12;
		uint32_t start = u32(f, g), end = u32(f, g + 4);
		if (cp < start)
			hi = mid;
		else if (cp > end)
			lo = mid + 1;
		else
			return u32(f, g + 8) + (cp - start);
	}
	return 0;
}

uint32_t tl_ttf_glyph(const struct tl_ttf *f, uint32_t cp)
{
	uint32_t g = f->cmap_fmt == 12 ? cmap12(f, cp) : cmap4(f, cp);
	return g < f->num_glyphs ? g : 0;
}

int tl_ttf_advance(const struct tl_ttf *f, uint32_t glyph)
{
	if (glyph >= f->num_hmetrics)
		glyph = f->num_hmetrics - 1u;
	return u16(f, f->hmtx + glyph * 4);
}

/* Byte range of a glyph in glyf; len 0 for an empty glyph. */
static int glyph_range(const struct tl_ttf *f, uint32_t glyph, size_t *off,
	size_t *len)
{
	size_t a, b;
	if (glyph >= f->num_glyphs)
		return -1;
	if (f->loca_long) {
		a = u32(f, f->loca + glyph * 4);
		b = u32(f, f->loca + glyph * 4 + 4);
	} else {
		a = (size_t)u16(f, f->loca + glyph * 2) * 2;
		b = (size_t)u16(f, f->loca + glyph * 2 + 2) * 2;
	}
	if (b < a || b > f->glyf_len)
		return -1;
	*off = f->glyf + a;
	*len = b - a;
	return 0;
}

static int grow(struct tl_outline *o, size_t points, size_t contours)
{
	if (points > TTF_MAX_POINTS)
		return -1;
	if (points > o->cap) {
		size_t cap = o->cap ? o->cap : 64;
		while (cap < points)
			cap *= 2;
		float *x = realloc(o->x, cap * sizeof(float));
		if (x == NULL)
			return -1;
		o->x = x;
		float *y = realloc(o->y, cap * sizeof(float));
		if (y == NULL)
			return -1;
		o->y = y;
		uint8_t *on = realloc(o->on, cap);
		if (on == NULL)
			return -1;
		o->on = on;
		o->cap = cap;
	}
	if (contours > o->ccap) {
		size_t cap = o->ccap ? o->ccap : 16;
		while (cap < contours)
			cap *= 2;
		uint16_t *e = realloc(o->ends, cap * sizeof(uint16_t));
		if (e == NULL)
			return -1;
		o->ends = e;
		o->ccap = cap;
	}
	return 0;
}

/* Transform for a composite component: x' = a x + c y + dx. */
struct xform {
	float a, b, c, d, dx, dy;
};

static int simple_glyph(const struct tl_ttf *f, size_t off, size_t len,
	int ncont, struct tl_outline *o, const struct xform *m)
{
	size_t end = off + len;
	size_t p = off + 10;
	size_t base = o->npoints;

	if (ncont == 0)
		return 0;
	if (p + (size_t)ncont * 2 + 2 > end)
		return -1;
	size_t npts = (size_t)u16(f, p + (size_t)(ncont - 1) * 2) + 1;
	if (grow(o, base + npts, o->ncontours + (size_t)ncont) != 0)
		return -1;
	for (int i = 0; i < ncont; i++) {
		size_t e = u16(f, p + (size_t)i * 2);
		if (e >= npts || (i > 0 && e <= (size_t)o->ends[o->ncontours - 1] - base))
			return -1;
		if (base + e > 0xffff)
			return -1;
		o->ends[o->ncontours++] = (uint16_t)(base + e);
	}
	p += (size_t)ncont * 2;
	p += 2 + u16(f, p); /* skip the hinting instructions */

	/* Flags, with repeats. */
	uint8_t *flags = o->on + base;
	for (size_t i = 0; i < npts;) {
		if (p >= end)
			return -1;
		uint8_t fl = f->data[p++];
		size_t rep = 1;
		if (fl & 8) {
			if (p >= end)
				return -1;
			rep += f->data[p++];
		}
		while (rep-- > 0 && i < npts)
			flags[i++] = fl;
	}
	/* x then y, each a byte or a word, signed by a flag, or repeated. */
	for (int axis = 0; axis < 2; axis++) {
		uint8_t short_bit = axis == 0 ? 2 : 4, same_bit = axis == 0 ? 16 : 32;
		float *out = axis == 0 ? o->x + base : o->y + base;
		int v = 0;
		for (size_t i = 0; i < npts; i++) {
			uint8_t fl = flags[i];
			if (fl & short_bit) {
				if (p >= end)
					return -1;
				int d = f->data[p++];
				v += (fl & same_bit) ? d : -d;
			} else if (!(fl & same_bit)) {
				if (p + 2 > end)
					return -1;
				v += s16(f, p);
				p += 2;
			}
			out[i] = (float)v;
		}
	}
	for (size_t i = 0; i < npts; i++) {
		float x = o->x[base + i], y = o->y[base + i];
		o->x[base + i] = m->a * x + m->c * y + m->dx;
		o->y[base + i] = m->b * x + m->d * y + m->dy;
		flags[i] &= 1;
	}
	o->npoints = base + npts;
	return 0;
}

static int load_glyph(const struct tl_ttf *f, uint32_t glyph,
	struct tl_outline *o, const struct xform *m, int depth);

static float f2dot14(const struct tl_ttf *f, size_t off)
{
	return (float)s16(f, off) / 16384.0f;
}

static int composite_glyph(const struct tl_ttf *f, size_t off, size_t len,
	struct tl_outline *o, const struct xform *m, int depth)
{
	size_t end = off + len;
	size_t p = off + 10;
	uint16_t flags;

	do {
		if (p + 4 > end)
			return -1;
		flags = u16(f, p);
		uint16_t child = u16(f, p + 2);
		p += 4;
		float dx, dy;
		if (flags & 1) { /* ARG_1_AND_2_ARE_WORDS */
			if (p + 4 > end)
				return -1;
			dx = s16(f, p);
			dy = s16(f, p + 2);
			p += 4;
		} else {
			if (p + 2 > end)
				return -1;
			dx = (int8_t)f->data[p];
			dy = (int8_t)f->data[p + 1];
			p += 2;
		}
		if (!(flags & 2)) /* point matching: rare; place at origin */
			dx = dy = 0;
		struct xform c = { 1, 0, 0, 1, dx, dy };
		if (flags & 8) { /* WE_HAVE_A_SCALE */
			if (p + 2 > end)
				return -1;
			c.a = c.d = f2dot14(f, p);
			p += 2;
		} else if (flags & 0x40) { /* X_AND_Y_SCALE */
			if (p + 4 > end)
				return -1;
			c.a = f2dot14(f, p);
			c.d = f2dot14(f, p + 2);
			p += 4;
		} else if (flags & 0x80) { /* TWO_BY_TWO */
			if (p + 8 > end)
				return -1;
			c.a = f2dot14(f, p);
			c.b = f2dot14(f, p + 2);
			c.c = f2dot14(f, p + 4);
			c.d = f2dot14(f, p + 6);
			p += 8;
		}
		/* Compose: apply c, then the parent's m. */
		struct xform t = {
			m->a * c.a + m->c * c.b, m->b * c.a + m->d * c.b,
			m->a * c.c + m->c * c.d, m->b * c.c + m->d * c.d,
			m->a * c.dx + m->c * c.dy + m->dx,
			m->b * c.dx + m->d * c.dy + m->dy,
		};
		if (load_glyph(f, child, o, &t, depth + 1) != 0)
			return -1;
	} while (flags & 0x20); /* MORE_COMPONENTS */
	return 0;
}

static int load_glyph(const struct tl_ttf *f, uint32_t glyph,
	struct tl_outline *o, const struct xform *m, int depth)
{
	size_t off, len;
	if (depth > TTF_MAX_DEPTH || glyph_range(f, glyph, &off, &len) != 0)
		return -1;
	if (len == 0)
		return 0;
	if (len < 10)
		return -1;
	int16_t ncont = s16(f, off);
	if (ncont >= 0)
		return simple_glyph(f, off, len, ncont, o, m);
	return composite_glyph(f, off, len, o, m, depth);
}

int tl_ttf_outline(const struct tl_ttf *f, uint32_t glyph,
	struct tl_outline *o)
{
	static const struct xform id = { 1, 0, 0, 1, 0, 0 };
	o->npoints = 0;
	o->ncontours = 0;
	if (load_glyph(f, glyph, o, &id, 0) != 0) {
		o->npoints = 0;
		o->ncontours = 0;
		return -1;
	}
	return 0;
}

void tl_outline_free(struct tl_outline *o)
{
	free(o->x);
	free(o->y);
	free(o->on);
	free(o->ends);
	memset(o, 0, sizeof(*o));
}
