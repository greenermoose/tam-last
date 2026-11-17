/* One monospace font at one size: cell metrics, grid fitting, and a cache
 * of rendered glyph masks. */
#ifndef TL_FONT_H
#define TL_FONT_H

#include <stddef.h>
#include <stdint.h>

#include "tam-last/ttf.h"

struct tl_glyph {
	uint32_t cp;    /* 0 marks an empty slot */
	int span;       /* cells wide: 1 or 2 */
	uint8_t *mask;  /* cell_w * span by cell_h coverage bytes */
};

struct tl_font {
	void *map;
	size_t map_len;
	struct tl_ttf ttf;
	char path[512];
	float px;       /* requested pixel size (em) */
	float scale_x;  /* font units to pixels across */
	float scale_y;  /* and down, after grid fitting */
	int cell_w, cell_h, baseline;
	float glyph_x;  /* left offset that centres the advance in a cell */
	struct tl_glyph *slots;
	size_t nslots, used;
	struct tl_outline scratch;
};

/* The font to use: path if given, else $TAM_LAST_FONT, else the first of
 * a short list of well-known monospace fonts that exists. NULL if none. */
const char *tl_font_find(const char *path);

/* Open a font file at pt points for dpi dots per inch. */
int tl_font_open(struct tl_font *f, const char *path, float pt, int dpi,
	char *err, size_t errlen);
/* The same for a font already in memory (data must outlive f). */
int tl_font_init(struct tl_font *f, const uint8_t *data, size_t len,
	float pt, int dpi, char *err, size_t errlen);
void tl_font_close(struct tl_font *f);

/* The rendered mask for cp, span cells wide. NULL only when out of
 * memory. */
const struct tl_glyph *tl_font_glyph(struct tl_font *f, uint32_t cp,
	int span);

#endif
