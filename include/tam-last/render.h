/* Drawing the grid into pixels. */
#ifndef TL_RENDER_H
#define TL_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "tam-last/font.h"
#include "tam-last/select.h"
#include "tam-last/term.h"

struct tl_palette {
	uint32_t fg, bg, cursor, cursor_text;
	uint32_t index[256];
};

/* The compiled-in colors: Tamlinux's current dark theme (Tokyo Night) for
 * the first 16, then the standard 6x6x6 cube and gray ramp. */
void tl_palette_default(struct tl_palette *p);
uint32_t tl_color_resolve(const struct tl_palette *p, uint32_t color,
	bool fg);

/* Draw t into a width x height XRGB8888 image with pad pixels of margin.
 * focused draws the active cursor, otherwise an outline. */
void tl_render(const struct tl_term *t, struct tl_font *f,
	const struct tl_palette *pal, uint32_t *pixels, int width, int height,
	int pad, bool focused);

/* Draw only the rows where rows[y] is true, or everything when full is true. */
void tl_render_rows(const struct tl_term *t, struct tl_font *f,
	const struct tl_palette *pal, uint32_t *pixels, int width, int height,
	int pad, bool focused, const struct tl_sel *sel, const bool *rows,
	bool full);

/* Draw a single-row status/overlay bar (e.g. for --keys) at grid row y_row. */
void tl_render_bar(struct tl_font *f, const struct tl_palette *pal,
	uint32_t *pixels, int width, int height, int pad, int y_row, int cols,
	const char *text);

struct tl_preedit {
	const char *text;       /* UTF-8 preedit text, or NULL/empty */
	int32_t cursor_begin;   /* byte offset in text, or -1 if hidden */
	int32_t cursor_end;     /* byte offset in text, or -1 if hidden */
};

/* Draw an active IME preedit string overlaid at the terminal cursor.
 * Returns the last grid row touched (or -1 if nothing was drawn). */
int tl_render_preedit(const struct tl_term *t, struct tl_font *f,
	const struct tl_palette *pal, uint32_t *pixels, int width, int height,
	int pad, const struct tl_preedit *pe);

#endif
