/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Grid to pixels. Writes OUTDIR/render-96dpi.ppm and render-120dpi.ppm,
 * sample screens drawn with the real terminal font when it is installed,
 * to look at by eye. */
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

	/* Underline and bar cursor styles, plus partial row rendering. */
	feed(&t, "\033[4 q");
	tl_render(&t, &font, &pal, px, w, h, pad, true);
	CHECK(px[(pad + font.cell_h - 1) * w + pad + 3 * font.cell_w + 1] == pal.cursor);
	CHECK(px[cy * w + pad + 3 * font.cell_w + 1] == pal.bg);
	feed(&t, "\033[6 q");
	tl_render(&t, &font, &pal, px, w, h, pad, true);
	CHECK(px[cy * w + pad + 3 * font.cell_w] == pal.cursor);

	/* Partial tl_render_rows: only row 1 drawn, row 0 left as sentinel. */
	px[pad * w + pad] = 0x123456;
	bool only_row1[2] = { false, true };
	tl_render_rows(&t, &font, &pal, px, w, h, pad, true, only_row1, false);
	CHECK(px[pad * w + pad] == 0x123456);
	free(px);
	tl_term_free(&t);
	tl_font_close(&font);

	/* Sample screens with the real font, at scale 1 (96 dpi) and at
	 * 1.25 (120 dpi, as on Fred's DP-1 and DP-2). */
	const char *path = tl_font_find(NULL);
	static const int dpis[] = { 96, 120 };
	for (int k = 0; path != NULL && k < 2; k++) {
		if (tl_font_open(&font, path, 9, dpis[k], err, sizeof(err)) != 0)
			break;
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
		snprintf(out, sizeof(out), "%s/render-%ddpi.ppm", outdir, dpis[k]);
		CHECK(write_ppm(out, px, w, h) == 0);
		printf("test_render: wrote %s (%s, cells of %dx%d px)\n", out,
			path, font.cell_w, font.cell_h);
		free(px);
		tl_term_free(&t);
		tl_font_close(&font);
	}

	CHECK_DONE("test_render");
}
