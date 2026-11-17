/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Grid to pixels. Writes OUTDIR/render.ppm, a sample screen drawn with
 * the real terminal font when it is installed, to look at by eye. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "tam-last/font.h"
#include "tam-last/render.h"
#include "tam-last/term.h"
#include "ttf_fixture.h"

static int write_ppm(const char *path, const uint32_t *px, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (f == NULL)
		return -1;
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++) {
		unsigned char rgb[3] = { (unsigned char)(px[i] >> 16),
			(unsigned char)(px[i] >> 8), (unsigned char)px[i] };
		fwrite(rgb, 1, 3, f);
	}
	return fclose(f);
}

static void feed(struct tl_term *t, const char *s)
{
	tl_term_feed(t, s, strlen(s));
}

int main(int argc, char **argv)
{
	const char *outdir = argc > 1 ? argv[1] : ".";
	struct fx fx;
	size_t len = fx_build(&fx);
	struct tl_font font;
	struct tl_term t;
	struct tl_palette pal;
	char err[200];

	setlocale(LC_CTYPE, "C.UTF-8");
	tl_palette_default(&pal);
	CHECK(pal.index[1] == 0xf7768e && pal.index[16] == 0 &&
		pal.index[231] == 0xffffff && pal.index[255] == 0xeeeeee);
	CHECK(tl_color_resolve(&pal, TL_COLOR_DEFAULT, true) == pal.fg);
	CHECK(tl_color_resolve(&pal, TL_COLOR_RGB(0x123456), false) == 0x123456);

	/* The fixture font: 'A' is a solid block, so pixels are predictable. */
	CHECK(tl_font_init(&font, fx.b, len, 9, 96, err, sizeof(err)) == 0);
	CHECK(tl_term_init(&t, 4, 2) == 0);
	feed(&t, "\033[31mA\033[0m \033[44m \033[0m");
	int pad = 2, w = 4 * font.cell_w + 2 * pad, h = 2 * font.cell_h + 2 * pad;
	uint32_t *px = calloc((size_t)w * (size_t)h, sizeof(*px));
	CHECK(px != NULL);
	tl_render(&t, &font, &pal, px, w, h, pad, true);
	int cy = pad + font.baseline - 2; /* inside the block */
	CHECK(px[0] == pal.bg);
	CHECK(px[cy * w + pad + font.cell_w / 2] == pal.index[1]);
	CHECK(px[cy * w + pad + 2 * font.cell_w + 1] == pal.index[4]);
	/* The cursor block sits after the last character. */
	CHECK(px[cy * w + pad + 3 * font.cell_w + 1] == pal.cursor);
	tl_render(&t, &font, &pal, px, w, h, pad, false);
	CHECK(px[(pad + 1) * w + pad + 3 * font.cell_w + 1] == pal.bg);
	CHECK(px[pad * w + pad + 3 * font.cell_w + 1] == pal.cursor);
	free(px);
	tl_term_free(&t);
	tl_font_close(&font);

	/* A sample screen with the real font. */
	const char *path = tl_font_find(NULL);
	if (path != NULL && tl_font_open(&font, path, 9, 96, err, sizeof(err)) == 0) {
		CHECK(tl_term_init(&t, 64, 12) == 0);
		feed(&t, "\033[32mfred@bosgame\033[0m:\033[34m~/Code/tamlinux/tam-last\033[0m$ ls --color\r\n"
			"\033[1;34march\033[0m  \033[1;34mbuild\033[0m  COPYRIGHT  LICENSE  Makefile  README\r\n"
			"\033[1;34msrc\033[0m   \033[1;34mtests\033[0m  \033[32mtools/pull-references\033[0m  VERSION\r\n"
			"\033[38;5;208m256 colours\033[0m, \033[38;2;180;120;250mdirect colour\033[0m, "
			"\033[4munderline\033[0m, \033[7minverse\033[0m\r\n"
			"Unicode: caf\xc3\xa9, \xce\xbb, \xe2\x86\x92, \xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80, "
			"\xe6\xbc\xa2\xe5\xad\x97, Nerd \xef\x80\x95\r\n"
			"The quick brown fox jumps over the lazy dog 0123456789\r\n"
			"il1| O0 {}[]() <>=! ->  ~`'\" @#$%^&*\r\n"
			"\033[32mfred@bosgame\033[0m:\033[34m~\033[0m$ ");
		w = t.cols * font.cell_w + 4;
		h = t.rows * font.cell_h + 4;
		px = calloc((size_t)w * (size_t)h, sizeof(*px));
		CHECK(px != NULL);
		tl_render(&t, &font, &pal, px, w, h, 2, true);
		char out[512];
		snprintf(out, sizeof(out), "%s/render.ppm", outdir);
		CHECK(write_ppm(out, px, w, h) == 0);
		printf("test_render: wrote %s (%s, %dx%d cells of %dx%d px)\n", out,
			path, t.cols, t.rows, font.cell_w, font.cell_h);
		free(px);
		tl_term_free(&t);
		tl_font_close(&font);
	}

	CHECK_DONE("test_render");
}
