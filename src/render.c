/* Software drawing: fill each cell's background, blend its glyph's
 * coverage mask in the foreground color, then combining marks and cursor. */
#include "tam-last/render.h"

static const uint32_t theme16[16] = {
	0x1a1b26, 0xf7768e, 0x9ece6a, 0xe0af68,
	0x7aa2f7, 0xad8ee6, 0x449dab, 0xa9b1d6,
	0x414868, 0xff7a93, 0xb9f27c, 0xff9e64,
	0x7da6ff, 0xbb9af7, 0x0db9d7, 0xc0caf5,
};

void tl_palette_default(struct tl_palette *p)
{
	static const uint8_t level[6] = { 0, 95, 135, 175, 215, 255 };
	p->fg = 0xa9b1d6;
	p->bg = 0x1a1b26;
	p->cursor = 0xc0caf5;
	p->cursor_text = 0x1a1b26;
	for (int i = 0; i < 16; i++)
		p->index[i] = theme16[i];
	for (int i = 0; i < 216; i++) {
		uint32_t r = level[i / 36], g = level[i / 6 % 6], b = level[i % 6];
		p->index[16 + i] = r << 16 | g << 8 | b;
	}
	for (int i = 0; i < 24; i++) {
		uint32_t v = 8 + 10 * (uint32_t)i;
		p->index[232 + i] = v << 16 | v << 8 | v;
	}
}

uint32_t tl_color_resolve(const struct tl_palette *p, uint32_t color, bool fg)
{
	switch (color >> 24) {
	case 1: return p->index[color & 0xff];
	case 2: return color & 0xffffff;
	default: return fg ? p->fg : p->bg;
	}
}

static uint32_t blend(uint32_t bg, uint32_t fg, unsigned a)
{
	uint32_t out = 0;
	for (int shift = 0; shift <= 16; shift += 8) {
		unsigned b = (bg >> shift) & 0xff, f = (fg >> shift) & 0xff;
		unsigned v = (b * (255 - a) + f * a + 127) / 255;
		out |= (uint32_t)v << shift;
	}
	return out;
}

static uint32_t dim(uint32_t c)
{
	return (c >> 1) & 0x7f7f7f;
}

static void rect(uint32_t *px, int width, int height, int x0, int y0,
	int w, int h, uint32_t color)
{
	for (int y = y0; y < y0 + h; y++) {
		if (y < 0 || y >= height)
			continue;
		for (int x = x0; x < x0 + w; x++)
			if (x >= 0 && x < width)
				px[(size_t)y * (size_t)width + (size_t)x] = color;
	}
}

static void draw_glyph(struct tl_font *f, uint32_t cp, int span, uint8_t style,
	uint32_t fg, uint32_t *pixels, int width, int height, int px0, int py0)
{
	const struct tl_glyph *g = tl_font_glyph_styled(f, cp, span, style);
	if (g == NULL)
		return;
	int gw = f->cell_w * span, ch = f->cell_h;
	for (int gy = 0; gy < ch; gy++) {
		int py = py0 + gy;
		if (py < 0 || py >= height)
			continue;
		uint32_t *row = pixels + (size_t)py * (size_t)width;
		const uint8_t *m = g->mask + (size_t)gy * (size_t)gw;
		for (int gx = 0; gx < gw && px0 + gx < width; gx++) {
			if (px0 + gx >= 0 && m[gx] != 0)
				row[px0 + gx] = blend(row[px0 + gx], fg, m[gx]);
		}
	}
}

void tl_render_rows(const struct tl_term *t, struct tl_font *f,
	const struct tl_palette *pal, uint32_t *pixels, int width, int height,
	int pad, bool focused, const struct tl_sel *sel, const bool *rows,
	bool full)
{
	int cw = f->cell_w, ch = f->cell_h;
	int line_h = ch / 14 > 0 ? ch / 14 : 1;
	int sb = t->alt_screen ? 0 : t->sb_len;
	int view_off = t->alt_screen ? 0 : t->view_offset;
	bool show_cursor = t->cursor_visible && view_off == 0;

	/* Margins and the leftover strip right and below the grid. */
	if (full)
		rect(pixels, width, height, 0, 0, width, height, pal->bg);

	for (int y = 0; y < t->rows; y++) {
		if (!full && rows != NULL && !rows[y])
			continue;
		int row_cols = 0;
		const struct tl_cell *row = tl_term_view_row(t, y, &row_cols);
		int abs_y = sb - view_off + y;
		for (int x = 0; x < t->cols; x++) {
			static const struct tl_cell blank_cell = {
				' ', { 0, 0 }, TL_COLOR_DEFAULT, TL_COLOR_DEFAULT, 0, 0
			};
			const struct tl_cell *c = (row != NULL && x < row_cols) ?
				&row[x] : &blank_cell;
			if (c->cp == TL_CELL_WIDE_TAIL)
				continue;
			int span = (row != NULL && x + 1 < t->cols && x + 1 < row_cols &&
				row[x + 1].cp == TL_CELL_WIDE_TAIL) ? 2 : 1;
			uint32_t fg = tl_color_resolve(pal, c->fg, true);
			uint32_t bg = tl_color_resolve(pal, c->bg, false);
			bool cursor_here = show_cursor && focused &&
				x == t->cx && y == t->cy;
			bool selected = sel != NULL && tl_sel_contains(sel, x, abs_y);
			if (((c->attrs & TL_ATTR_INVERSE) != 0) ^ selected) {
				uint32_t tmp = fg; fg = bg; bg = tmp;
			}
			if (cursor_here && t->cursor_style == TL_CURSOR_BLOCK) {
				fg = pal->cursor_text;
				bg = pal->cursor;
			}
			if (c->attrs & TL_ATTR_DIM)
				fg = dim(fg);
			int px0 = pad + x * cw, py0 = pad + y * ch;
			rect(pixels, width, height, px0, py0, cw * span, ch, bg);
			if (c->cp != ' ' && !(c->attrs & TL_ATTR_HIDDEN)) {
				uint8_t style = 0;
				if (c->attrs & TL_ATTR_BOLD)
					style |= TL_STYLE_BOLD;
				if (c->attrs & TL_ATTR_ITALIC)
					style |= TL_STYLE_ITALIC;
				draw_glyph(f, c->cp, span, style, fg, pixels,
					width, height, px0, py0);
				for (uint8_t i = 0; i < c->ncomb; i++)
					draw_glyph(f, c->comb[i], span, style,
						fg, pixels, width, height, px0,
						py0);
			}
			if (c->attrs & TL_ATTR_UNDERLINE)
				rect(pixels, width, height, px0, py0 + f->baseline + line_h,
					cw * span, line_h, fg);
			if (c->attrs & TL_ATTR_STRIKE)
				rect(pixels, width, height, px0, py0 + ch / 2, cw * span,
					line_h, fg);
			if (cursor_here && t->cursor_style == TL_CURSOR_UNDERLINE)
				rect(pixels, width, height, px0, py0 + ch - line_h,
					cw * span, line_h, pal->cursor);
			else if (cursor_here && t->cursor_style == TL_CURSOR_BAR)
				rect(pixels, width, height, px0, py0, line_h,
					ch, pal->cursor);
		}
		/* Unfocused (or over a wide tail): an outline box. */
		if (show_cursor && !focused && y == t->cy &&
		    t->cx < t->cols && t->cy < t->rows) {
			int x0 = pad + t->cx * cw, y0 = pad + t->cy * ch;
			rect(pixels, width, height, x0, y0, cw, 1, pal->cursor);
			rect(pixels, width, height, x0, y0 + ch - 1, cw, 1, pal->cursor);
			rect(pixels, width, height, x0, y0, 1, ch, pal->cursor);
			rect(pixels, width, height, x0 + cw - 1, y0, 1, ch, pal->cursor);
		}
	}
}

void tl_render(const struct tl_term *t, struct tl_font *f,
	const struct tl_palette *pal, uint32_t *pixels, int width, int height,
	int pad, bool focused)
{
	tl_render_rows(t, f, pal, pixels, width, height, pad, focused, NULL,
		NULL, true);
}
