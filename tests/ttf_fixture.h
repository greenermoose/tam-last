/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A tiny TrueType font built in memory, so the font tests need no font
 * files. Glyphs: 0 empty (.notdef), 1 a 500 x 700 square ('A'), 2 a
 * composite of glyph 1 moved right by 100 ('B'), 3 a contour of control
 * points only ('C'). 1000 units per em. */
#ifndef TAM_LAST_TESTS_TTF_FIXTURE_H
#define TAM_LAST_TESTS_TTF_FIXTURE_H

#include <stdint.h>
#include <string.h>

struct fx {
	uint8_t b[4096];
	size_t n;
};

static void fx16(struct fx *f, unsigned v)
{
	f->b[f->n++] = (uint8_t)(v >> 8);
	f->b[f->n++] = (uint8_t)v;
}

static void fx32(struct fx *f, uint32_t v)
{
	fx16(f, v >> 16);
	fx16(f, v & 0xffff);
}

static void fx_pad(struct fx *f)
{
	while (f->n % 4)
		f->b[f->n++] = 0;
}

static void fx_put16(struct fx *f, size_t at, unsigned v)
{
	f->b[at] = (uint8_t)(v >> 8);
	f->b[at + 1] = (uint8_t)v;
}

static void fx_put32(struct fx *f, size_t at, uint32_t v)
{
	fx_put16(f, at, v >> 16);
	fx_put16(f, at + 2, v & 0xffff);
}

/* Build the font; returns its length. */
static size_t fx_build(struct fx *f)
{
	static const char *tags[] = { "OS/2", "cmap", "glyf", "head", "hhea",
		"hmtx", "loca", "maxp" };
	enum { NT = 8 };
	size_t dir;

	memset(f, 0, sizeof(*f));
	fx32(f, 0x00010000);
	fx16(f, NT);
	fx16(f, 128); fx16(f, 3); fx16(f, 0);
	dir = f->n;
	f->n += NT * 16;

	size_t off[NT], len[NT];
	uint32_t glyph_off[5];
	for (int t = 0; t < NT; t++) {
		fx_pad(f);
		off[t] = f->n;
		const char *tag = tags[t];
		if (strcmp(tag, "OS/2") == 0) {
			fx16(f, 2); /* version */
			while (f->n < off[t] + 86)
				fx16(f, 0);
			fx16(f, 500); /* sxHeight */
			fx16(f, 700); /* sCapHeight */
			fx16(f, 0); fx16(f, 0); fx16(f, 0);
		} else if (strcmp(tag, "cmap") == 0) {
			fx16(f, 0); fx16(f, 1);
			fx16(f, 3); fx16(f, 1); fx32(f, 12);
			/* format 4: one segment 'A'..'C' -> 1..3, plus 0xFFFF */
			fx16(f, 4); fx16(f, 32); fx16(f, 0);
			fx16(f, 4); fx16(f, 4); fx16(f, 1); fx16(f, 0);
			fx16(f, 'C'); fx16(f, 0xffff); /* ends */
			fx16(f, 0);
			fx16(f, 'A'); fx16(f, 0xffff); /* starts */
			fx16(f, (unsigned)(1 - 'A') & 0xffff); fx16(f, 1); /* deltas */
			fx16(f, 0); fx16(f, 0); /* range offsets */
		} else if (strcmp(tag, "glyf") == 0) {
			glyph_off[0] = 0;
			glyph_off[1] = 0;
			/* glyph 1: square */
			fx16(f, 1); fx16(f, 0); fx16(f, 0); fx16(f, 500); fx16(f, 700);
			fx16(f, 3); /* end of contour 0 */
			fx16(f, 0); /* no instructions */
			for (int i = 0; i < 4; i++)
				f->b[f->n++] = 1; /* on curve, word coordinates */
			fx16(f, 0); fx16(f, 500); fx16(f, 0); fx16(f, (unsigned)-500 & 0xffff);
			fx16(f, 0); fx16(f, 0); fx16(f, 700); fx16(f, 0);
			fx_pad(f);
			glyph_off[2] = (uint32_t)(f->n - off[t]);
			/* glyph 2: composite of 1, dx = 100 */
			fx16(f, (unsigned)-1 & 0xffff);
			fx16(f, 100); fx16(f, 0); fx16(f, 600); fx16(f, 700);
			fx16(f, 0x0003); /* words, xy values */
			fx16(f, 1);
			fx16(f, 100); fx16(f, 0);
			fx_pad(f);
			glyph_off[3] = (uint32_t)(f->n - off[t]);
			/* glyph 3: four control points round (250, 350) */
			fx16(f, 1); fx16(f, 0); fx16(f, 0); fx16(f, 500); fx16(f, 700);
			fx16(f, 3);
			fx16(f, 0);
			for (int i = 0; i < 4; i++)
				f->b[f->n++] = 0;
			fx16(f, 0); fx16(f, 250); fx16(f, 250); fx16(f, (unsigned)-250 & 0xffff);
			fx16(f, 350); fx16(f, 350); fx16(f, (unsigned)-350 & 0xffff);
			fx16(f, (unsigned)-350 & 0xffff);
			fx_pad(f);
			glyph_off[4] = (uint32_t)(f->n - off[t]);
		} else if (strcmp(tag, "head") == 0) {
			fx32(f, 0x00010000); fx32(f, 0); fx32(f, 0); fx32(f, 0x5F0F3CF5);
			fx16(f, 0); fx16(f, 1000);
			while (f->n < off[t] + 50)
				fx16(f, 0);
			fx16(f, 1); /* long loca */
			fx16(f, 0);
		} else if (strcmp(tag, "hhea") == 0) {
			fx32(f, 0x00010000);
			fx16(f, 800); fx16(f, (unsigned)-200 & 0xffff); fx16(f, 0);
			while (f->n < off[t] + 34)
				fx16(f, 0);
			fx16(f, 4); /* numberOfHMetrics */
		} else if (strcmp(tag, "hmtx") == 0) {
			for (int i = 0; i < 4; i++) {
				fx16(f, 600);
				fx16(f, 0);
			}
		} else if (strcmp(tag, "loca") == 0) {
			for (int i = 0; i < 5; i++)
				fx32(f, glyph_off[i]);
		} else if (strcmp(tag, "maxp") == 0) {
			fx32(f, 0x00005000);
			fx16(f, 4);
		}
		len[t] = f->n - off[t];
	}
	fx_pad(f);
	for (int t = 0; t < NT; t++) {
		size_t r = dir + (size_t)t * 16;
		memcpy(f->b + r, tags[t], 4);
		fx_put32(f, r + 4, 0);
		fx_put32(f, r + 8, (uint32_t)off[t]);
		fx_put32(f, r + 12, (uint32_t)len[t]);
	}
	return f->n;
}

#endif
