/* TrueType fonts with glyf outlines: table parsing, character map, and
 * outlines. The font file is untrusted input; every read is checked
 * against the file's length. */
#ifndef TL_TTF_H
#define TL_TTF_H

#include <stddef.h>
#include <stdint.h>

struct tl_ttf {
	const uint8_t *data;
	size_t len;
	uint32_t glyf, glyf_len, loca, loca_len, hmtx, hmtx_len;
	uint32_t cmap;     /* offset of the chosen cmap subtable */
	uint16_t cmap_fmt; /* 4 or 12 */
	int loca_long;
	uint16_t num_glyphs, num_hmetrics, units_per_em;
	int16_t ascender, descender, line_gap;
	int16_t x_height, cap_height; /* 0 when OS/2 has no value */
};

/* A glyph's contours in font units, y up. Off-curve points are
 * quadratic control points, as in the glyf table. */
struct tl_outline {
	float *x, *y;
	uint8_t *on;
	size_t npoints, cap;
	uint16_t *ends; /* index of each contour's last point */
	size_t ncontours, ccap;
};

/* Parse the font in data (which must outlive f). Returns 0, or -1 with a
 * message in err. */
int tl_ttf_load(struct tl_ttf *f, const uint8_t *data, size_t len,
	char *err, size_t errlen);
/* Glyph index for a code point; 0 (the missing glyph) when not mapped. */
uint32_t tl_ttf_glyph(const struct tl_ttf *f, uint32_t cp);
/* Advance width in font units. */
int tl_ttf_advance(const struct tl_ttf *f, uint32_t glyph);
/* Fill o with the glyph's outline, composites resolved. Returns 0, or -1
 * on malformed data or a limit. An empty glyph (space) gives 0 points. */
int tl_ttf_outline(const struct tl_ttf *f, uint32_t glyph,
	struct tl_outline *o);
void tl_outline_free(struct tl_outline *o);

#endif
