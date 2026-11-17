/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The TrueType parser and the rasterizer, on a font built in memory, on
 * damaged copies of it, and on the real terminal font when installed. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "tam-last/font.h"
#include "tam-last/raster.h"
#include "tam-last/ttf.h"
#include "ttf_fixture.h"

static long mask_sum(const uint8_t *m, int w, int h)
{
	long s = 0;
	for (int i = 0; i < w * h; i++)
		s += m[i];
	return s;
}

int main(void)
{
	struct fx fx;
	size_t len = fx_build(&fx);
	struct tl_ttf f;
	char err[160];
	struct tl_outline o = { 0 };

	int loaded = tl_ttf_load(&f, fx.b, len, err, sizeof(err));
	if (loaded != 0)
		fprintf(stderr, "  fixture: %s\n", err);
	CHECK(loaded == 0);
	CHECK(f.units_per_em == 1000 && f.num_glyphs == 4);
	CHECK(f.x_height == 500 && f.ascender == 800 && f.descender == -200);
	CHECK(tl_ttf_glyph(&f, 'A') == 1 && tl_ttf_glyph(&f, 'C') == 3);
	CHECK(tl_ttf_glyph(&f, 'Z') == 0 && tl_ttf_glyph(&f, 0x1F600) == 0);
	CHECK(tl_ttf_advance(&f, 1) == 600);

	CHECK(tl_ttf_outline(&f, 1, &o) == 0);
	CHECK(o.npoints == 4 && o.ncontours == 1 && o.ends[0] == 3);
	CHECK(o.x[2] == 500 && o.y[2] == 700);
	CHECK(tl_ttf_outline(&f, 2, &o) == 0); /* the composite */
	CHECK(o.npoints == 4 && o.x[0] == 100 && o.x[1] == 600);
	CHECK(tl_ttf_outline(&f, 0, &o) == 0 && o.npoints == 0);

	/* Exact area: a 10 x 14 pixel square covers 140 pixels' worth, wherever
	 * it sits on the grid. */
	uint8_t mask[32 * 32];
	CHECK(tl_ttf_outline(&f, 1, &o) == 0);
	CHECK(tl_raster(&o, 0.02f, 0.02f, 4.0f, 20.0f, mask, 32, 32) == 0);
	CHECK(labs(mask_sum(mask, 32, 32) - 140 * 255) <= 2);
	CHECK(mask[10 * 32 + 8] == 255 && mask[2 * 32 + 2] == 0);
	CHECK(tl_raster(&o, 0.02f, 0.02f, 4.5f, 20.25f, mask, 32, 32) == 0);
	CHECK(labs(mask_sum(mask, 32, 32) - 140 * 255) <= 140);
	CHECK(mask[10 * 32 + 4] > 100 && mask[10 * 32 + 4] < 155); /* half */
	/* Clipped at every edge without writing outside the mask. */
	CHECK(tl_raster(&o, 0.1f, 0.1f, -10.0f, 40.0f, mask, 32, 32) == 0);
	CHECK(mask[31 * 32 + 0] == 255 && mask[31 * 32 + 31] == 255);
	CHECK(mask[0] == 255); /* the square runs off the top too */

	/* Only control points: a rounded shape, smaller than its box. */
	CHECK(tl_ttf_outline(&f, 3, &o) == 0 && o.npoints == 4);
	CHECK(tl_raster(&o, 0.02f, 0.02f, 4.0f, 20.0f, mask, 32, 32) == 0);
	long round_area = mask_sum(mask, 32, 32) / 255;
	CHECK(round_area > 40 && round_area < 140);

	/* Every truncation of the font, and random damage, fails safely. */
	uint8_t copy[sizeof(fx.b)];
	for (size_t cut = 0; cut < len; cut++) {
		memcpy(copy, fx.b, cut);
		if (tl_ttf_load(&f, copy, cut, err, sizeof(err)) == 0)
			for (uint32_t g = 0; g < 4; g++)
				(void)tl_ttf_outline(&f, g, &o);
	}
	srand(2);
	for (int round = 0; round < 3000; round++) {
		memcpy(copy, fx.b, len);
		for (int k = 0; k < 4; k++)
			copy[(size_t)rand() % len] = (uint8_t)rand();
		if (tl_ttf_load(&f, copy, len, err, sizeof(err)) != 0)
			continue;
		for (uint32_t cp = 'A'; cp <= 'D'; cp++) {
			uint32_t g = tl_ttf_glyph(&f, cp);
			if (tl_ttf_outline(&f, g, &o) == 0)
				(void)tl_raster(&o, 0.02f, 0.02f, 4, 20, mask, 32, 32);
		}
	}
	CHECK(tl_ttf_load(&f, (const uint8_t *)"OTTO", 4, err, sizeof(err)) != 0);

	/* Grid fitting on the fixture: 12 px em, x-height 500 -> 6 px. */
	struct tl_font font;
	CHECK(tl_font_init(&font, fx.b, len, 9, 96, err, sizeof(err)) == 0);
	CHECK(fabsf(500 * font.scale_y - 6.0f) < 0.001f);
	CHECK(fabsf(font.scale_x - 0.012f) < 0.0001f); /* across: untouched */
	CHECK(font.cell_w > 0 && font.cell_h == font.baseline + 3);
	const struct tl_glyph *g = tl_font_glyph(&font, 'A', 1);
	CHECK(g != NULL && mask_sum(g->mask, font.cell_w, font.cell_h) > 0);
	CHECK(tl_font_glyph(&font, 'A', 1) == g); /* cached */
	for (uint32_t cp = 1; cp < 3000; cp++) /* forces the cache to grow */
		CHECK(tl_font_glyph(&font, cp, 1 + (int)(cp % 2)) != NULL);
	tl_font_close(&font);

	/* The real font, when it is here. */
	const char *real = "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf";
	if (access(real, R_OK) == 0) {
		CHECK(tl_font_open(&font, real, 9, 96, err, sizeof(err)) == 0);
		printf("test_ttf: JetBrains Mono at 9 pt: cell %dx%d, baseline %d,"
			" x-height %.2f px\n", font.cell_w, font.cell_h,
			font.baseline, font.ttf.x_height * font.scale_y);
		CHECK(font.cell_w == 7); /* as foot draws it at 9 pt */
		CHECK(font.cell_h >= 13 && font.cell_h <= 20);
		g = tl_font_glyph(&font, 'g', 1);
		CHECK(g != NULL && mask_sum(g->mask, font.cell_w, font.cell_h) > 0);
		g = tl_font_glyph(&font, 0xF015, 1); /* a Nerd Font icon */
		CHECK(g != NULL);
		tl_font_close(&font);
	} else {
		printf("test_ttf: %s not installed; skipped\n", real);
	}

	tl_outline_free(&o);
	CHECK_DONE("test_ttf");
}
