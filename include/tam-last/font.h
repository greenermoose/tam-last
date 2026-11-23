/* One monospace font family at one size: cell metrics, grid fitting,
 * bold/italic sibling faces, fallback fonts, procedural box/block/Braille
 * glyphs, and a cache of rendered glyph masks. */
#ifndef TL_FONT_H
#define TL_FONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tam-last/ttf.h"

enum {
	TL_STYLE_REGULAR = 0,
	TL_STYLE_BOLD = 1,
	TL_STYLE_ITALIC = 2,
	TL_STYLE_BOLD_ITALIC = 3
};

#define TL_FONT_FALLBACK_MAX 4

struct tl_face {
	void *map;
	size_t map_len;
	struct tl_ttf ttf;
	float scale_x;
	float scale_y;
	float glyph_x;
	bool loaded;
};

struct tl_glyph {
	uint32_t cp;    /* 0 marks an empty slot */
	int span;       /* cells wide: 1 or 2 */
	uint8_t style;  /* TL_STYLE_* */
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
	struct tl_face faces[4]; /* indices 1..3: bold, italic, bold-italic */
	struct tl_face fallbacks[TL_FONT_FALLBACK_MAX];
	size_t nfallbacks;
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

/* Load an optional styled face (TL_STYLE_BOLD, TL_STYLE_ITALIC,
 * TL_STYLE_BOLD_ITALIC) from disk or memory. Returns 0 on success, -1 on error. */
int tl_font_load_face(struct tl_font *f, uint8_t style, const char *path);
int tl_font_load_face_mem(struct tl_font *f, uint8_t style,
	const uint8_t *data, size_t len);
/* Auto-discover sibling Bold/Italic/BoldItalic files next to f->path for any
 * style slots not already loaded. */
void tl_font_load_siblings(struct tl_font *f);

/* Add an optional fallback TrueType font from disk or memory. */
int tl_font_add_fallback(struct tl_font *f, const char *path);
int tl_font_add_fallback_mem(struct tl_font *f, const uint8_t *data,
	size_t len);
/* Probe installed TrueType fallback fonts (symbols, Noto Sans, DejaVu, etc.). */
void tl_font_load_default_fallbacks(struct tl_font *f);

/* The rendered mask for cp, span cells wide, with style (TL_STYLE_*).
 * NULL only when out of memory. */
const struct tl_glyph *tl_font_glyph_styled(struct tl_font *f, uint32_t cp,
	int span, uint8_t style);
const struct tl_glyph *tl_font_glyph(struct tl_font *f, uint32_t cp,
	int span);

/* Shared font cache for multi-window server mode: keyed by font paths,
 * point size (in tenths of a point), and dpi. Keeps up to TL_FONT_CACHE_WARM
 * unreferenced entries warm so reopening a window stays instant. */
#define TL_FONT_CACHE_WARM 8

struct tl_font_key {
	char path[512];
	char bold[512];
	char italic[512];
	char bold_italic[512];
	char fallback[512];
	int pt_tenths;
	int dpi;
};

struct tl_font_cache_entry {
	struct tl_font_key key;
	struct tl_font font;
	int refcount;
	uint64_t last_used;
};

struct tl_font_cache {
	struct tl_font_cache_entry **entries;
	size_t len;
	size_t cap;
	uint64_t clock;
};

void tl_font_cache_init(struct tl_font_cache *c);
struct tl_font *tl_font_cache_acquire(struct tl_font_cache *c,
	const struct tl_font_key *key, char *err, size_t errlen);
void tl_font_cache_release(struct tl_font_cache *c, struct tl_font *f);
void tl_font_cache_free(struct tl_font_cache *c);

#endif
