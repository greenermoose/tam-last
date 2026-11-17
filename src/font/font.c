/* Font lookup, cell metrics with vertical grid fitting, and the glyph
 * cache.
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
	struct stat st;
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
	    st.st_size < 12) {
		snprintf(err, errlen, "cannot read font %s", path);
		if (fd >= 0)
			close(fd);
		return -1;
	}
	size_t len = (size_t)st.st_size;
	void *map = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) {
		snprintf(err, errlen, "cannot map font %s", path);
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
	if (f->map != NULL)
		munmap(f->map, f->map_len);
	memset(f, 0, sizeof(*f));
}

static size_t slot_of(const struct tl_font *f, uint32_t cp, int span)
{
	uint32_t h = (cp * 2654435761u) ^ (uint32_t)span;
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
		size_t j = slot_of(f, old[i].cp, old[i].span);
		while (f->slots[j].cp != 0)
			j = (j + 1) & (n - 1);
		f->slots[j] = old[i];
	}
	free(old);
	return 0;
}

const struct tl_glyph *tl_font_glyph(struct tl_font *f, uint32_t cp,
	int span)
{
	if (cp == 0)
		cp = ' ';
	size_t i = slot_of(f, cp, span);
	while (f->slots[i].cp != 0) {
		if (f->slots[i].cp == cp && f->slots[i].span == span)
			return &f->slots[i];
		i = (i + 1) & (f->nslots - 1);
	}

	if (f->used + 1 > f->nslots * 7 / 10) {
		if (f->nslots >= CACHE_MAX) {
			cache_clear(f); /* rare: start again */
		} else if (cache_grow(f) != 0) {
			return NULL;
		}
		i = slot_of(f, cp, span);
		while (f->slots[i].cp != 0)
			i = (i + 1) & (f->nslots - 1);
	}

	int w = f->cell_w * span, h = f->cell_h;
	uint8_t *mask = malloc((size_t)w * (size_t)h);
	if (mask == NULL)
		return NULL;
	uint32_t g = tl_ttf_glyph(&f->ttf, cp);
	/* A malformed glyph draws as nothing rather than failing. */
	if (tl_ttf_outline(&f->ttf, g, &f->scratch) != 0 ||
	    tl_raster(&f->scratch, f->scale_x, f->scale_y,
		    f->glyph_x + (float)(f->cell_w * (span - 1)) / 2,
		    (float)f->baseline, mask, w, h) != 0)
		memset(mask, 0, (size_t)w * (size_t)h);
	f->slots[i] = (struct tl_glyph){ cp, span, mask };
	f->used++;
	return &f->slots[i];
}
