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
#include "tam-last/box.h"
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
	tl_render_rows(&t, &font, &pal, px, w, h, pad, true, NULL, only_row1, false);
	CHECK(px[pad * w + pad] == 0x123456);

	/* Selection highlights by inverting fg and bg. */
	struct tl_sel sel;
	tl_sel_start(&sel, &t, 0, 0, TL_SEL_CHAR);
	tl_sel_end(&sel);
	tl_render_rows(&t, &font, &pal, px, w, h, pad, true, &sel, NULL, true);
	CHECK(px[cy * w + pad + font.cell_w / 2] == pal.bg);

	/* Scrollback rendering hides the live cursor and shows history. */
	feed(&t, "\r\nbc\r\nde");
	CHECK(t.sb_len == 1);
	tl_term_scroll_view(&t, 1);
	tl_render_rows(&t, &font, &pal, px, w, h, pad, true, NULL, NULL, true);
	CHECK(px[cy * w + pad + font.cell_w / 2] == pal.index[1]);

	/* Overlay bar rendering (--keys): fills bar strip and draws glyphs. */
	{
		int bh = 3 * font.cell_h + 2 * pad;
		uint32_t *bpx = calloc((size_t)w * (size_t)bh, sizeof(*bpx));
		CHECK(bpx != NULL);
		tl_render_bar(&font, &pal, bpx, w, bh, pad, 2, 4, "A   extra");
		int bar_y = pad + 2 * font.cell_h;
		CHECK(bpx[bar_y * w + 0] == pal.index[8]);
		CHECK(bpx[(bar_y + font.baseline - 2) * w + pad + font.cell_w / 2] == pal.index[15]);
		/* Out-of-bounds y_row is a safe no-op. */
		tl_render_bar(&font, &pal, bpx, w, bh, pad, 99, 4, "clipped");
		free(bpx);
	}

	/* IME preedit overlay rendering: underline, selection highlight, bar/hidden cursor. */
	{
		struct tl_term pt;
		CHECK(tl_term_init(&pt, 6, 2) == 0);
		int pw = 6 * font.cell_w + 2 * pad;
		int ph = 2 * font.cell_h + 2 * pad;
		uint32_t *ppx = calloc((size_t)pw * (size_t)ph, sizeof(*ppx));
		CHECK(ppx != NULL);
		tl_render(&pt, &font, &pal, ppx, pw, ph, pad, true);

		/* Empty/NULL preedit is a no-op returning -1. */
		struct tl_preedit pe_empty = { NULL, 0, 0 };
		CHECK(tl_render_preedit(&pt, &font, &pal, ppx, pw, ph, pad, &pe_empty) == -1);

		/* Normal preedit with selection on second char ("AA", sel 1..2). */
		struct tl_preedit pe_sel = { "AA", 1, 2 };
		CHECK(tl_render_preedit(&pt, &font, &pal, ppx, pw, ph, pad, &pe_sel) == 0);
		int line_h = font.cell_h / 14 > 0 ? font.cell_h / 14 : 1;
		int ul_y = pad + font.baseline + line_h;
		/* Cell 0: normal bg, fg glyph + fg underline. */
		CHECK(ppx[cy * pw + pad + font.cell_w / 2] == pal.fg);
		CHECK(ppx[ul_y * pw + pad + font.cell_w / 2] == pal.fg);
		/* Cell 1: inverted (fg background, bg glyph). */
		CHECK(ppx[pad * pw + pad + font.cell_w + 1] == pal.fg);
		CHECK(ppx[cy * pw + pad + font.cell_w + font.cell_w / 2] == pal.bg);

		/* Bar cursor at end of preedit (cursor_begin == cursor_end == 1). */
		tl_render(&pt, &font, &pal, ppx, pw, ph, pad, false);
		struct tl_preedit pe_bar = { "A", 1, 1 };
		CHECK(tl_render_preedit(&pt, &font, &pal, ppx, pw, ph, pad, &pe_bar) == 0);
		CHECK(ppx[cy * pw + pad + font.cell_w] == pal.cursor);

		/* Hidden cursor (-1, -1) draws no bar cursor at cell 1. */
		tl_render(&pt, &font, &pal, ppx, pw, ph, pad, false);
		struct tl_preedit pe_hid = { "A", -1, -1 };
		CHECK(tl_render_preedit(&pt, &font, &pal, ppx, pw, ph, pad, &pe_hid) == 0);
		CHECK(ppx[cy * pw + pad + font.cell_w] == pal.bg);

		/* Wrapping across row boundary returns row 1. */
		feed(&pt, "12345"); /* cx == 5 */
		struct tl_preedit pe_wrap = { "AB", 0, 0 };
		CHECK(tl_render_preedit(&pt, &font, &pal, ppx, pw, ph, pad, &pe_wrap) == 1);

		free(ppx);
		tl_term_free(&pt);
	}
	free(px);
	tl_term_free(&t);
	tl_font_close(&font);

	/* Procedural box drawing, block elements, and Braille patterns. */
	{
		int bw = 10, bh = 20;
		uint8_t m[200];
		CHECK(!tl_box_draw('A', m, bw, bh));
		CHECK(tl_box_draw(0x2500, m, bw, bh)); /* ─ */
		CHECK(m[(bh / 2) * bw + 0] == 255);
		CHECK(m[(bh / 2) * bw + (bw - 1)] == 255);
		CHECK(m[0] == 0);
		CHECK(tl_box_draw(0x2502, m, bw, bh)); /* │ */
		CHECK(m[0 * bw + bw / 2] == 255);
		CHECK(m[(bh - 1) * bw + bw / 2] == 255);
		CHECK(m[0] == 0);
		CHECK(tl_box_draw(0x253C, m, bw, bh)); /* ┼ */
		CHECK(m[(bh / 2) * bw + 0] == 255 && m[0 * bw + bw / 2] == 255);
		CHECK(tl_box_draw(0x256D, m, bw, bh)); /* ╭ */
		CHECK(m[(bh / 2) * bw + (bw - 1)] == 255 &&
			m[(bh - 1) * bw + bw / 2] == 255);
		CHECK(tl_box_draw(0x2571, m, bw, bh)); /* ╱ */
		CHECK(m[(bh / 2) * bw + bw / 2] > 0);
		CHECK(tl_box_draw(0x2588, m, bw, bh)); /* █ */
		CHECK(m[0] == 255 && m[bw * bh - 1] == 255);
		CHECK(tl_box_draw(0x2584, m, bw, bh)); /* ▄ */
		CHECK(m[0] == 0 && m[(bh - 1) * bw] == 255);
		CHECK(tl_box_draw(0x2592, m, bw, bh)); /* ▒ */
		CHECK(m[0] == 128 && m[bw * bh - 1] == 128);
		CHECK(tl_box_draw(0x259A, m, bw, bh)); /* ▚ */
		CHECK(m[0] == 255 && m[bw - 1] == 0 &&
			m[(bh - 1) * bw] == 0 && m[bw * bh - 1] == 255);
		CHECK(tl_box_draw(0x2800, m, bw, bh)); /* ⠀ */
		int sum0 = 0;
		for (int i = 0; i < bw * bh; i++)
			sum0 += m[i];
		CHECK(sum0 == 0);
		CHECK(tl_box_draw(0x28FF, m, bw, bh)); /* ⣿ */
		int sum_all = 0;
		for (int i = 0; i < bw * bh; i++)
			sum_all += m[i];
		CHECK(sum_all > 0);
		CHECK(tl_box_draw(0x2801, m, bw, bh)); /* ⠁ */
		int sum_one = 0;
		for (int i = 0; i < bw * bh; i++)
			sum_one += m[i];
		CHECK(sum_one > 0 && sum_one < sum_all);
	}

	/* Sample screens with the real font, at scale 1 (96 dpi) and at
	 * 1.25 (120 dpi, as on Fred's DP-1 and DP-2). */
	const char *path = tl_font_find(NULL);
	static const int dpis[] = { 96, 120 };
	for (int k = 0; path != NULL && k < 2; k++) {
		if (tl_font_open(&font, path, 9, dpis[k], err, sizeof(err)) != 0)
			break;
		tl_font_load_siblings(&font);
		tl_font_load_default_fallbacks(&font);
		if (font.faces[TL_STYLE_BOLD].loaded) {
			const struct tl_glyph *gr = tl_font_glyph_styled(&font,
				'M', 1, TL_STYLE_REGULAR);
			const struct tl_glyph *gb = tl_font_glyph_styled(&font,
				'M', 1, TL_STYLE_BOLD);
			CHECK(gr != NULL && gb != NULL);
			CHECK(memcmp(gr->mask, gb->mask,
				(size_t)font.cell_w * (size_t)font.cell_h) != 0);
		}
		if (font.faces[TL_STYLE_ITALIC].loaded) {
			const struct tl_glyph *gr = tl_font_glyph_styled(&font,
				'a', 1, TL_STYLE_REGULAR);
			const struct tl_glyph *gi = tl_font_glyph_styled(&font,
				'a', 1, TL_STYLE_ITALIC);
			CHECK(gr != NULL && gi != NULL);
			CHECK(memcmp(gr->mask, gi->mask,
				(size_t)font.cell_w * (size_t)font.cell_h) != 0);
		}
		/* Zero-advance combining mark (U+0301) must land inside the cell. */
		{
			const struct tl_glyph *gc = tl_font_glyph(&font, 0x0301, 1);
			CHECK(gc != NULL);
			int sum = 0;
			for (int i = 0; i < font.cell_w * font.cell_h; i++)
				sum += gc->mask[i];
			CHECK(sum > 0);
		}
		CHECK(tl_term_init(&t, 64, 14) == 0);
		feed(&t, "\033[32mfred@bosgame\033[0m:\033[34m~/Code/tamlinux/tam-last\033[0m$ ls --color\r\n"
			"\033[1;34march\033[0m  \033[1;34mbuild\033[0m  COPYRIGHT  LICENSE  Makefile  README\r\n"
			"\033[1mbold\033[0m  \033[3mitalic\033[0m  \033[1;3mbold-italic\033[0m  \033[4munderline\033[0m  \033[7minverse\033[0m\r\n"
			"\033[38;5;208m256 colours\033[0m, \033[38;2;180;120;250mdirect colour\033[0m\r\n"
			"\xe2\x94\x8c\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x90 \xe2\x95\x94\xe2\x95\x90\xe2\x95\x97 \xe2\x95\xad\xe2\x94\x80\xe2\x95\xae \xe2\x96\x80\xe2\x96\x84\xe2\x96\x88\xe2\x96\x91\xe2\x96\x92\xe2\x96\x93 \xe2\xa0\x81\xe2\xa0\x8b\xe2\xa3\xbf\r\n"
			"\xe2\x94\x94\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x98 \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x9d \xe2\x95\xb0\xe2\x94\x80\xe2\x95\xaf \xe2\x95\xb1\xe2\x95\xb2\xe2\x95\xb3\r\n"
			"Unicode: caf\xc3\xa9, \xce\xbb, \xe2\x86\x92, \xe2\x98\x85, Nerd \xef\x80\x95\r\n"
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
