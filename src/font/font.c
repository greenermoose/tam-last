/* Font lookup, cell metrics with vertical grid fitting, sibling faces,
 * fallback fonts, procedural box/block/Braille glyphs, and the glyph cache.
 *
 * Grid fitting: there is no bytecode interpreter. Instead the vertical
 * scale is nudged so the font's x-height is a whole number of pixels, and
 * the cell and baseline are whole pixels, so lower-case tops and the
 * baseline land on pixel edges. Horizontal scale is left alone, so cells
 * are as wide as other terminals make them. That is most of what
 * FreeType's own TrueType hinting (interpreter v40, vertical only) does
 * for a modern font at terminal sizes. */
#include "tam-last/font.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tam-last/box.h"
#include "tam-last/raster.h"

#define CACHE_MAX 16384

static const char *const known_fonts[] = {
	"/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf",
	"/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
	"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
	"/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
	"/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
	"/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
	NULL
};

static const char *const known_fallbacks[] = {
	"/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
	"/usr/share/fonts/noto/NotoSansSymbols-Regular.ttf",
	"/usr/share/fonts/noto/NotoSans-Regular.ttf",
	"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
	"/usr/share/fonts/TTF/DejaVuSans.ttf",
	"/usr/share/fonts/droid/DroidSansFallbackFull.ttf",
	NULL
};

const char *tl_font_find(const char *path)
{
	if (path != NULL && path[0] != '\0')
		return path;
	const char *env = getenv("TAM_LAST_FONT");
	if (env != NULL && env[0] != '\0')
		return env;
	for (size_t i = 0; known_fonts[i] != NULL; i++)
		if (access(known_fonts[i], R_OK) == 0)
			return known_fonts[i];
	return NULL;
}

static int map_font_file(const char *path, void **out_map, size_t *out_len)
{
	struct stat st;
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
	    st.st_size < 12) {
		if (fd >= 0)
			close(fd);
		return -1;
	}
	size_t len = (size_t)st.st_size;
	void *map = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED)
		return -1;
	*out_map = map;
	*out_len = len;
	return 0;
}

static void face_metrics(const struct tl_font *f, struct tl_face *face)
{
	const struct tl_ttf *t = &face->ttf;
	float scale = f->px / (float)t->units_per_em;
	face->scale_x = scale;
	if (t->x_height > 0) {
		float xh = (float)t->x_height * scale;
		float fitted = roundf(xh);
		if (fitted >= 1)
			scale *= fitted / xh;
	}
	face->scale_y = scale;
	float adv = (float)tl_ttf_advance(t, tl_ttf_glyph(t, 'M')) *
		face->scale_x;
	face->glyph_x = ((float)f->cell_w - adv) / 2.0f;
}

static void face_close(struct tl_face *face)
{
	if (face->map != NULL)
		munmap(face->map, face->map_len);
	memset(face, 0, sizeof(*face));
}

int tl_font_init(struct tl_font *f, const uint8_t *data, size_t len,
	float pt, int dpi, char *err, size_t errlen)
{
	memset(f, 0, sizeof(*f));
	if (tl_ttf_load(&f->ttf, data, len, err, errlen) != 0)
		return -1;
	const struct tl_ttf *t = &f->ttf;
	f->px = pt * (float)dpi / 72.0f;
	if (f->px < 4 || f->px > 512) {
		snprintf(err, errlen, "font size out of range");
		return -1;
	}
	float scale = f->px / (float)t->units_per_em;
	f->scale_x = scale;
	if (t->x_height > 0) {
		float xh = (float)t->x_height * scale;
		float fitted = roundf(xh);
		if (fitted >= 1)
			scale *= fitted / xh;
	}
	f->scale_y = scale;

	int ascent = (int)ceilf((float)t->ascender * scale);
	int descent = (int)ceilf(-(float)t->descender * scale);
	int gap = (int)roundf((float)(t->line_gap > 0 ? t->line_gap : 0) * scale);
	f->cell_h = ascent + descent + gap;
	f->baseline = ascent + gap / 2;

	float adv = (float)tl_ttf_advance(t, tl_ttf_glyph(t, 'M')) * f->scale_x;
	f->cell_w = (int)roundf(adv);
	if (f->cell_w < 1)
		f->cell_w = 1;
	if (f->cell_h < 1)
		f->cell_h = 1;
	f->glyph_x = ((float)f->cell_w - adv) / 2;

	f->nslots = 512;
	f->slots = calloc(f->nslots, sizeof(*f->slots));
	if (f->slots == NULL) {
		snprintf(err, errlen, "out of memory");
		return -1;
	}
	return 0;
}

int tl_font_open(struct tl_font *f, const char *path, float pt, int dpi,
	char *err, size_t errlen)
{
	void *map = NULL;
	size_t len = 0;
	if (map_font_file(path, &map, &len) != 0) {
		snprintf(err, errlen, "cannot read font %s", path);
		return -1;
	}
	char why[160];
	if (tl_font_init(f, map, len, pt, dpi, why, sizeof(why)) != 0) {
		snprintf(err, errlen, "%s: %s", path, why);
		munmap(map, len);
		return -1;
	}
	f->map = map;
	f->map_len = len;
	snprintf(f->path, sizeof(f->path), "%s", path);
	return 0;
}

int tl_font_load_face_mem(struct tl_font *f, uint8_t style,
	const uint8_t *data, size_t len)
{
	if (style < 1 || style > 3 || data == NULL || len < 12)
		return -1;
	struct tl_face tmp;
	memset(&tmp, 0, sizeof(tmp));
	char err[128];
	if (tl_ttf_load(&tmp.ttf, data, len, err, sizeof(err)) != 0)
		return -1;
	face_metrics(f, &tmp);
	tmp.loaded = true;
	face_close(&f->faces[style]);
	f->faces[style] = tmp;
	return 0;
}

int tl_font_load_face(struct tl_font *f, uint8_t style, const char *path)
{
	if (style < 1 || style > 3 || path == NULL || path[0] == '\0')
		return -1;
	void *map = NULL;
	size_t len = 0;
	if (map_font_file(path, &map, &len) != 0)
		return -1;
	if (tl_font_load_face_mem(f, style, map, len) != 0) {
		munmap(map, len);
		return -1;
	}
	f->faces[style].map = map;
	f->faces[style].map_len = len;
	return 0;
}

static bool try_sibling(struct tl_font *f, uint8_t style, const char *stem,
	size_t stem_len, const char *suffix)
{
	char cand[600];
	int n = snprintf(cand, sizeof(cand), "%.*s%s.ttf", (int)stem_len, stem,
		suffix);
	if (n <= 0 || (size_t)n >= sizeof(cand))
		return false;
	return tl_font_load_face(f, style, cand) == 0;
}

void tl_font_load_siblings(struct tl_font *f)
{
	if (f->path[0] == '\0')
		return;
	size_t len = strlen(f->path);
	if (len < 5 || strcmp(f->path + len - 4, ".ttf") != 0)
		return;

	size_t stem_len = len - 4;
	const char *reg = "-Regular";
	size_t reg_len = strlen(reg);
	if (stem_len > reg_len &&
	    memcmp(f->path + stem_len - reg_len, reg, reg_len) == 0)
		stem_len -= reg_len;

	if (!f->faces[TL_STYLE_BOLD].loaded)
		(void)try_sibling(f, TL_STYLE_BOLD, f->path, stem_len, "-Bold");
	if (!f->faces[TL_STYLE_ITALIC].loaded) {
		if (!try_sibling(f, TL_STYLE_ITALIC, f->path, stem_len,
			    "-Italic"))
			(void)try_sibling(f, TL_STYLE_ITALIC, f->path,
				stem_len, "-Oblique");
	}
	if (!f->faces[TL_STYLE_BOLD_ITALIC].loaded) {
		if (!try_sibling(f, TL_STYLE_BOLD_ITALIC, f->path, stem_len,
			    "-BoldItalic"))
			(void)try_sibling(f, TL_STYLE_BOLD_ITALIC, f->path,
				stem_len, "-BoldOblique");
	}
}

int tl_font_add_fallback_mem(struct tl_font *f, const uint8_t *data,
	size_t len)
{
	if (f->nfallbacks >= TL_FONT_FALLBACK_MAX || data == NULL || len < 12)
		return -1;
	struct tl_face tmp;
	memset(&tmp, 0, sizeof(tmp));
	char err[128];
	if (tl_ttf_load(&tmp.ttf, data, len, err, sizeof(err)) != 0)
		return -1;
	face_metrics(f, &tmp);
	tmp.loaded = true;
	f->fallbacks[f->nfallbacks++] = tmp;
	return 0;
}

int tl_font_add_fallback(struct tl_font *f, const char *path)
{
	if (f->nfallbacks >= TL_FONT_FALLBACK_MAX || path == NULL ||
	    path[0] == '\0')
		return -1;
	if (f->path[0] != '\0' && strcmp(path, f->path) == 0)
		return -1;
	void *map = NULL;
	size_t len = 0;
	if (map_font_file(path, &map, &len) != 0)
		return -1;
	if (tl_font_add_fallback_mem(f, map, len) != 0) {
		munmap(map, len);
		return -1;
	}
	f->fallbacks[f->nfallbacks - 1].map = map;
	f->fallbacks[f->nfallbacks - 1].map_len = len;
	return 0;
}

void tl_font_load_default_fallbacks(struct tl_font *f)
{
	for (size_t i = 0; known_fallbacks[i] != NULL &&
	     f->nfallbacks < TL_FONT_FALLBACK_MAX; i++)
		(void)tl_font_add_fallback(f, known_fallbacks[i]);
}

static void cache_clear(struct tl_font *f)
{
	for (size_t i = 0; i < f->nslots; i++) {
		free(f->slots[i].mask);
		f->slots[i] = (struct tl_glyph){ 0 };
	}
	f->used = 0;
}

void tl_font_close(struct tl_font *f)
{
	if (f->slots != NULL) {
		cache_clear(f);
		free(f->slots);
	}
	tl_outline_free(&f->scratch);
	for (int i = 1; i < 4; i++)
		face_close(&f->faces[i]);
	for (size_t i = 0; i < f->nfallbacks; i++)
		face_close(&f->fallbacks[i]);
	if (f->map != NULL)
		munmap(f->map, f->map_len);
	memset(f, 0, sizeof(*f));
}

static size_t slot_of(const struct tl_font *f, uint32_t cp, int span,
	uint8_t style)
{
	uint32_t h = (cp * 2654435761u) ^ ((uint32_t)span * 40503u) ^
		((uint32_t)style * 2246822519u);
	return h & (f->nslots - 1);
}

static int cache_grow(struct tl_font *f)
{
	size_t n = f->nslots * 2;
	struct tl_glyph *s = calloc(n, sizeof(*s));
	if (s == NULL)
		return -1;
	struct tl_glyph *old = f->slots;
	size_t oldn = f->nslots;
	f->slots = s;
	f->nslots = n;
	for (size_t i = 0; i < oldn; i++) {
		if (old[i].cp == 0)
			continue;
		size_t j = slot_of(f, old[i].cp, old[i].span, old[i].style);
		while (f->slots[j].cp != 0)
			j = (j + 1) & (n - 1);
		f->slots[j] = old[i];
	}
	free(old);
	return 0;
}

static const struct tl_face *pick_styled_face(const struct tl_font *f,
	uint8_t style)
{
	if (style == TL_STYLE_BOLD_ITALIC) {
		if (f->faces[TL_STYLE_BOLD_ITALIC].loaded)
			return &f->faces[TL_STYLE_BOLD_ITALIC];
		if (f->faces[TL_STYLE_BOLD].loaded)
			return &f->faces[TL_STYLE_BOLD];
		if (f->faces[TL_STYLE_ITALIC].loaded)
			return &f->faces[TL_STYLE_ITALIC];
		return NULL;
	}
	if (style == TL_STYLE_BOLD && f->faces[TL_STYLE_BOLD].loaded)
		return &f->faces[TL_STYLE_BOLD];
	if (style == TL_STYLE_ITALIC && f->faces[TL_STYLE_ITALIC].loaded)
		return &f->faces[TL_STYLE_ITALIC];
	return NULL;
}

const struct tl_glyph *tl_font_glyph_styled(struct tl_font *f, uint32_t cp,
	int span, uint8_t style)
{
	if (cp == 0)
		cp = ' ';
	style &= 3u;
	size_t i = slot_of(f, cp, span, style);
	while (f->slots[i].cp != 0) {
		if (f->slots[i].cp == cp && f->slots[i].span == span &&
		    f->slots[i].style == style)
			return &f->slots[i];
		i = (i + 1) & (f->nslots - 1);
	}

	if (f->used + 1 > f->nslots * 7 / 10) {
		if (f->nslots >= CACHE_MAX) {
			cache_clear(f); /* rare: start again */
		} else if (cache_grow(f) != 0) {
			return NULL;
		}
		i = slot_of(f, cp, span, style);
		while (f->slots[i].cp != 0)
			i = (i + 1) & (f->nslots - 1);
	}

	int w = f->cell_w * span, h = f->cell_h;
	uint8_t *mask = malloc((size_t)w * (size_t)h);
	if (mask == NULL)
		return NULL;

	if (!tl_box_draw(cp, mask, w, h)) {
		const struct tl_ttf *ttf = &f->ttf;
		float sx = f->scale_x, sy = f->scale_y;
		float gx = f->glyph_x + (float)(f->cell_w * (span - 1)) / 2.0f;
		uint32_t g = 0;

		const struct tl_face *styled = pick_styled_face(f, style);
		if (styled != NULL) {
			g = tl_ttf_glyph(&styled->ttf, cp);
			if (g != 0) {
				ttf = &styled->ttf;
				sx = styled->scale_x;
				sy = styled->scale_y;
				gx = styled->glyph_x +
					(float)(f->cell_w * (span - 1)) / 2.0f;
			}
		}
		if (g == 0)
			g = tl_ttf_glyph(&f->ttf, cp);
		if (g == 0) {
			for (size_t k = 0; k < f->nfallbacks; k++) {
				const struct tl_face *fb = &f->fallbacks[k];
				uint32_t fg = tl_ttf_glyph(&fb->ttf, cp);
				if (fg != 0) {
					ttf = &fb->ttf;
					g = fg;
					sx = fb->scale_x;
					sy = fb->scale_y;
					float adv = (float)tl_ttf_advance(ttf, g) * sx;
					if (adv > (float)w && adv > 0.0f) {
						float fit = (float)w / adv;
						sx *= fit;
						sy *= fit;
						adv = (float)w;
					}
					gx = adv > 0.0f ?
						((float)w - adv) / 2.0f : 0.0f;
					break;
				}
			}
		}
		/* A malformed glyph draws as nothing rather than failing. */
		if (tl_ttf_outline(ttf, g, &f->scratch) != 0) {
			memset(mask, 0, (size_t)w * (size_t)h);
		} else {
			/* Zero-advance combining marks often have outlines left
			 * of x=0 (for post-advance placement); shift into cell. */
			if (tl_ttf_advance(ttf, g) == 0 &&
			    f->scratch.npoints > 0) {
				float xmax = f->scratch.x[0];
				for (size_t p = 1; p < f->scratch.npoints; p++)
					if (f->scratch.x[p] > xmax)
						xmax = f->scratch.x[p];
				if (xmax <= 0.0f)
					gx += (float)w;
			}
			if (tl_raster(&f->scratch, sx, sy, gx,
				    (float)f->baseline, mask, w, h) != 0)
				memset(mask, 0, (size_t)w * (size_t)h);
		}
	}
	f->slots[i] = (struct tl_glyph){ cp, span, style, mask };
	f->used++;
	return &f->slots[i];
}

const struct tl_glyph *tl_font_glyph(struct tl_font *f, uint32_t cp,
	int span)
{
	return tl_font_glyph_styled(f, cp, span, TL_STYLE_REGULAR);
}

static bool font_key_eq(const struct tl_font_key *a,
	const struct tl_font_key *b)
{
	return a->pt_tenths == b->pt_tenths && a->dpi == b->dpi &&
		strcmp(a->path, b->path) == 0 &&
		strcmp(a->bold, b->bold) == 0 &&
		strcmp(a->italic, b->italic) == 0 &&
		strcmp(a->bold_italic, b->bold_italic) == 0 &&
		strcmp(a->fallback, b->fallback) == 0;
}

static void font_cache_evict(struct tl_font_cache *c)
{
	size_t warm = 0;
	for (size_t i = 0; i < c->len; i++) {
		if (c->entries[i]->refcount == 0)
			warm++;
	}
	while (warm > TL_FONT_CACHE_WARM) {
		size_t victim = (size_t)-1;
		for (size_t i = 0; i < c->len; i++) {
			if (c->entries[i]->refcount != 0)
				continue;
			if (victim == (size_t)-1 ||
			    c->entries[i]->last_used <
			    c->entries[victim]->last_used)
				victim = i;
		}
		if (victim == (size_t)-1)
			break;
		tl_font_close(&c->entries[victim]->font);
		free(c->entries[victim]);
		c->entries[victim] = c->entries[--c->len];
		warm--;
	}
}

void tl_font_cache_init(struct tl_font_cache *c)
{
	memset(c, 0, sizeof(*c));
}

struct tl_font *tl_font_cache_acquire(struct tl_font_cache *c,
	const struct tl_font_key *key, char *err, size_t errlen)
{
	if (c == NULL || key == NULL || key->path[0] == '\0') {
		if (err != NULL && errlen > 0)
			snprintf(err, errlen, "missing font path");
		return NULL;
	}
	for (size_t i = 0; i < c->len; i++) {
		if (font_key_eq(&c->entries[i]->key, key)) {
			c->entries[i]->refcount++;
			c->entries[i]->last_used = ++c->clock;
			return &c->entries[i]->font;
		}
	}

	struct tl_font_cache_entry *e = calloc(1, sizeof(*e));
	if (e == NULL) {
		if (err != NULL && errlen > 0)
			snprintf(err, errlen, "out of memory");
		return NULL;
	}
	float pt = (float)key->pt_tenths / 10.0f;
	if (tl_font_open(&e->font, key->path, pt, key->dpi, err, errlen) != 0) {
		free(e);
		return NULL;
	}
	if (key->bold[0] != '\0')
		(void)tl_font_load_face(&e->font, TL_STYLE_BOLD, key->bold);
	if (key->italic[0] != '\0')
		(void)tl_font_load_face(&e->font, TL_STYLE_ITALIC, key->italic);
	if (key->bold_italic[0] != '\0')
		(void)tl_font_load_face(&e->font, TL_STYLE_BOLD_ITALIC,
			key->bold_italic);
	tl_font_load_siblings(&e->font);
	if (key->fallback[0] != '\0')
		(void)tl_font_add_fallback(&e->font, key->fallback);
	tl_font_load_default_fallbacks(&e->font);

	if (c->len == c->cap) {
		size_t ncap = c->cap == 0 ? 8 : c->cap * 2;
		struct tl_font_cache_entry **ne = realloc(c->entries,
			ncap * sizeof(*ne));
		if (ne == NULL) {
			tl_font_close(&e->font);
			free(e);
			if (err != NULL && errlen > 0)
				snprintf(err, errlen, "out of memory");
			return NULL;
		}
		c->entries = ne;
		c->cap = ncap;
	}
	e->key = *key;
	e->refcount = 1;
	e->last_used = ++c->clock;
	c->entries[c->len++] = e;
	return &e->font;
}

void tl_font_cache_release(struct tl_font_cache *c, struct tl_font *f)
{
	if (c == NULL || f == NULL)
		return;
	for (size_t i = 0; i < c->len; i++) {
		if (&c->entries[i]->font == f) {
			if (c->entries[i]->refcount > 0)
				c->entries[i]->refcount--;
			c->entries[i]->last_used = ++c->clock;
			font_cache_evict(c);
			return;
		}
	}
}

void tl_font_cache_free(struct tl_font_cache *c)
{
	if (c == NULL)
		return;
	for (size_t i = 0; i < c->len; i++) {
		tl_font_close(&c->entries[i]->font);
		free(c->entries[i]);
	}
	free(c->entries);
	memset(c, 0, sizeof(*c));
}
