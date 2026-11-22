/* Procedural rendering of Unicode Box Drawing (U+2500..U+257F), Block
 * Elements (U+2580..U+259F), and Braille Patterns (U+2800..U+28FF). */
#include "tam-last/box.h"

#include <math.h>
#include <string.h>

static void fill_rect(uint8_t *mask, int w, int h, int x0, int y0, int x1,
	int y1, uint8_t cov)
{
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > w)
		x1 = w;
	if (y1 > h)
		y1 = h;
	for (int y = y0; y < y1; y++) {
		uint8_t *row = mask + (size_t)y * (size_t)w;
		for (int x = x0; x < x1; x++) {
			if (cov > row[x])
				row[x] = cov;
		}
	}
}

/* Line weight per arm: 0=none, 1=light, 2=heavy, 3=double. */
enum { W_NONE = 0, W_LIGHT = 1, W_HEAVY = 2, W_DOUBLE = 3 };

struct box_arms {
	uint8_t l, r, u, d;
};

/* Pack 4 arms (l, r, u, d) into one byte: l | (r<<2) | (u<<4) | (d<<6). */
#define ARMS(l, r, u, d) \
	((uint8_t)((l) | ((r) << 2) | ((u) << 4) | ((d) << 6)))

static const uint8_t box_table[128] = {
	/* 2500..2503 */
	[0x00] = ARMS(1, 1, 0, 0), /* ─ */
	[0x01] = ARMS(2, 2, 0, 0), /* ━ */
	[0x02] = ARMS(0, 0, 1, 1), /* │ */
	[0x03] = ARMS(0, 0, 2, 2), /* ┃ */
	/* 2504..250B: dashed (handled separately) */
	/* 250C..251B: corners */
	[0x0C] = ARMS(0, 1, 0, 1), /* ┌ */
	[0x0D] = ARMS(0, 2, 0, 1), /* ┍ */
	[0x0E] = ARMS(0, 1, 0, 2), /* ┎ */
	[0x0F] = ARMS(0, 2, 0, 2), /* ┏ */
	[0x10] = ARMS(1, 0, 0, 1), /* ┐ */
	[0x11] = ARMS(2, 0, 0, 1), /* ┑ */
	[0x12] = ARMS(1, 0, 0, 2), /* ┒ */
	[0x13] = ARMS(2, 0, 0, 2), /* ┓ */
	[0x14] = ARMS(0, 1, 1, 0), /* └ */
	[0x15] = ARMS(0, 2, 1, 0), /* ┕ */
	[0x16] = ARMS(0, 1, 2, 0), /* ┖ */
	[0x17] = ARMS(0, 2, 2, 0), /* ┗ */
	[0x18] = ARMS(1, 0, 1, 0), /* ┘ */
	[0x19] = ARMS(2, 0, 1, 0), /* ┙ */
	[0x1A] = ARMS(1, 0, 2, 0), /* ┚ */
	[0x1B] = ARMS(2, 0, 2, 0), /* ┛ */
	/* 251C..252B: vertical tees */
	[0x1C] = ARMS(0, 1, 1, 1), /* ├ */
	[0x1D] = ARMS(0, 2, 1, 1), /* ┝ */
	[0x1E] = ARMS(0, 1, 2, 1), /* ┞ */
	[0x1F] = ARMS(0, 1, 1, 2), /* ┟ */
	[0x20] = ARMS(0, 1, 2, 2), /* ┠ */
	[0x21] = ARMS(0, 2, 2, 1), /* ┡ */
	[0x22] = ARMS(0, 2, 1, 2), /* ┢ */
	[0x23] = ARMS(0, 2, 2, 2), /* ┣ */
	[0x24] = ARMS(1, 0, 1, 1), /* ┤ */
	[0x25] = ARMS(2, 0, 1, 1), /* ┥ */
	[0x26] = ARMS(1, 0, 2, 1), /* ┦ */
	[0x27] = ARMS(1, 0, 1, 2), /* ┧ */
	[0x28] = ARMS(1, 0, 2, 2), /* ┨ */
	[0x29] = ARMS(2, 0, 2, 1), /* ┩ */
	[0x2A] = ARMS(2, 0, 1, 2), /* ┪ */
	[0x2B] = ARMS(2, 0, 2, 2), /* ┫ */
	/* 252C..253B: horizontal tees */
	[0x2C] = ARMS(1, 1, 0, 1), /* ┬ */
	[0x2D] = ARMS(2, 1, 0, 1), /* ┭ */
	[0x2E] = ARMS(1, 2, 0, 1), /* ┮ */
	[0x2F] = ARMS(2, 2, 0, 1), /* ┯ */
	[0x30] = ARMS(1, 1, 0, 2), /* ┰ */
	[0x31] = ARMS(2, 1, 0, 2), /* ┱ */
	[0x32] = ARMS(1, 2, 0, 2), /* ┲ */
	[0x33] = ARMS(2, 2, 0, 2), /* ┳ */
	[0x34] = ARMS(1, 1, 1, 0), /* ┴ */
	[0x35] = ARMS(2, 1, 1, 0), /* ┵ */
	[0x36] = ARMS(1, 2, 1, 0), /* ┶ */
	[0x37] = ARMS(2, 2, 1, 0), /* ┷ */
	[0x38] = ARMS(1, 1, 2, 0), /* ┸ */
	[0x39] = ARMS(2, 1, 2, 0), /* ┹ */
	[0x3A] = ARMS(1, 2, 2, 0), /* ┺ */
	[0x3B] = ARMS(2, 2, 2, 0), /* ┻ */
	/* 253C..254B: crosses */
	[0x3C] = ARMS(1, 1, 1, 1), /* ┼ */
	[0x3D] = ARMS(2, 1, 1, 1), /* ┽ */
	[0x3E] = ARMS(1, 2, 1, 1), /* ┾ */
	[0x3F] = ARMS(2, 2, 1, 1), /* ┿ */
	[0x40] = ARMS(1, 1, 2, 1), /* ╀ */
	[0x41] = ARMS(1, 1, 1, 2), /* ╁ */
	[0x42] = ARMS(1, 1, 2, 2), /* ╂ */
	[0x43] = ARMS(2, 1, 2, 1), /* ╃ */
	[0x44] = ARMS(1, 2, 2, 1), /* ╄ */
	[0x45] = ARMS(2, 1, 1, 2), /* ╅ */
	[0x46] = ARMS(1, 2, 1, 2), /* ╆ */
	[0x47] = ARMS(2, 2, 2, 1), /* ╇ */
	[0x48] = ARMS(2, 2, 1, 2), /* ╈ */
	[0x49] = ARMS(2, 1, 2, 2), /* ╉ */
	[0x4A] = ARMS(1, 2, 2, 2), /* ╊ */
	[0x4B] = ARMS(2, 2, 2, 2), /* ╋ */
	/* 254C..254F: 2-dash (handled separately) */
	/* 2550..256C: double and mixed double/single */
	[0x50] = ARMS(3, 3, 0, 0), /* ═ */
	[0x51] = ARMS(0, 0, 3, 3), /* ║ */
	[0x52] = ARMS(0, 3, 0, 1), /* ╒ */
	[0x53] = ARMS(0, 1, 0, 3), /* ╓ */
	[0x54] = ARMS(0, 3, 0, 3), /* ╔ */
	[0x55] = ARMS(3, 0, 0, 1), /* ╕ */
	[0x56] = ARMS(1, 0, 0, 3), /* ╖ */
	[0x57] = ARMS(3, 0, 0, 3), /* ╗ */
	[0x58] = ARMS(0, 3, 1, 0), /* ╘ */
	[0x59] = ARMS(0, 1, 3, 0), /* ╙ */
	[0x5A] = ARMS(0, 3, 3, 0), /* ╚ */
	[0x5B] = ARMS(3, 0, 1, 0), /* ╛ */
	[0x5C] = ARMS(1, 0, 3, 0), /* ╜ */
	[0x5D] = ARMS(3, 0, 3, 0), /* ╝ */
	[0x5E] = ARMS(0, 3, 1, 1), /* ╞ */
	[0x5F] = ARMS(0, 1, 3, 3), /* ╟ */
	[0x60] = ARMS(0, 3, 3, 3), /* ╠ */
	[0x61] = ARMS(3, 0, 1, 1), /* ╡ */
	[0x62] = ARMS(1, 0, 3, 3), /* ╢ */
	[0x63] = ARMS(3, 0, 3, 3), /* ╣ */
	[0x64] = ARMS(3, 3, 0, 1), /* ╤ */
	[0x65] = ARMS(1, 1, 0, 3), /* ╥ */
	[0x66] = ARMS(3, 3, 0, 3), /* ╦ */
	[0x67] = ARMS(3, 3, 1, 0), /* ╧ */
	[0x68] = ARMS(1, 1, 3, 0), /* ╨ */
	[0x69] = ARMS(3, 3, 3, 0), /* ╩ */
	[0x6A] = ARMS(3, 3, 1, 1), /* ╪ */
	[0x6B] = ARMS(1, 1, 3, 3), /* ╫ */
	[0x6C] = ARMS(3, 3, 3, 3), /* ╬ */
	/* 256D..2573: arcs and diagonals (handled separately) */
	/* 2574..257F: half lines and mixed straight lines */
	[0x74] = ARMS(1, 0, 0, 0), /* ╴ */
	[0x75] = ARMS(0, 0, 1, 0), /* ╵ */
	[0x76] = ARMS(0, 1, 0, 0), /* ╶ */
	[0x77] = ARMS(0, 0, 0, 1), /* ╷ */
	[0x78] = ARMS(2, 0, 0, 0), /* ╸ */
	[0x79] = ARMS(0, 0, 2, 0), /* ╹ */
	[0x7A] = ARMS(0, 2, 0, 0), /* ╺ */
	[0x7B] = ARMS(0, 0, 0, 2), /* ╻ */
	[0x7C] = ARMS(1, 2, 0, 0), /* ╼ */
	[0x7D] = ARMS(0, 0, 1, 2), /* ╽ */
	[0x7E] = ARMS(2, 1, 0, 0), /* ╾ */
	[0x7F] = ARMS(0, 0, 2, 1), /* ╿ */
};

static void draw_arms(uint8_t *mask, int w, int h, uint8_t packed)
{
	struct box_arms a = {
		packed & 3u,
		(packed >> 2) & 3u,
		(packed >> 4) & 3u,
		(packed >> 6) & 3u,
	};
	int lt = h / 14;
	if (lt < 1)
		lt = 1;
	int ht = lt * 2;
	if (ht < 2)
		ht = 2;
	int gap = lt;

	int cx = w / 2, cy = h / 2;
	int lx0 = cx - lt / 2, lx1 = lx0 + lt;
	int ly0 = cy - lt / 2, ly1 = ly0 + lt;
	int hx0 = cx - ht / 2, hx1 = hx0 + ht;
	int hy0 = cy - ht / 2, hy1 = hy0 + ht;

	/* Double line tracks: two lt-wide bands separated by gap. */
	int vx1 = cx - lt - gap / 2, vx2 = vx1 + lt + gap;
	int hy_top = cy - lt - gap / 2, hy_bot = hy_top + lt + gap;

	/* Center bounding box covered by vertical / horizontal arms. */
	int v_left = lx0, v_right = lx1;
	if (a.u == W_DOUBLE || a.d == W_DOUBLE) {
		v_left = vx1;
		v_right = vx2 + lt;
	} else if (a.u == W_HEAVY || a.d == W_HEAVY) {
		v_left = hx0;
		v_right = hx1;
	}
	int h_top = ly0, h_bot = ly1;
	if (a.l == W_DOUBLE || a.r == W_DOUBLE) {
		h_top = hy_top;
		h_bot = hy_bot + lt;
	} else if (a.l == W_HEAVY || a.r == W_HEAVY) {
		h_top = hy0;
		h_bot = hy1;
	}

	/* Left arm */
	if (a.l == W_LIGHT) {
		fill_rect(mask, w, h, 0, ly0, v_right, ly1, 255);
	} else if (a.l == W_HEAVY) {
		fill_rect(mask, w, h, 0, hy0, v_right, hy1, 255);
	} else if (a.l == W_DOUBLE) {
		int top_end = (a.u == W_DOUBLE) ? vx1 + lt :
			      (a.d == W_DOUBLE && a.r != W_DOUBLE) ? vx2 + lt :
			      (a.u == W_LIGHT || a.d == W_LIGHT) ? lx1 : cx;
		int bot_end = (a.d == W_DOUBLE) ? vx1 + lt :
			      (a.u == W_DOUBLE && a.r != W_DOUBLE) ? vx2 + lt :
			      (a.u == W_LIGHT || a.d == W_LIGHT) ? lx1 : cx;
		if (a.r == W_DOUBLE && a.u != W_DOUBLE)
			top_end = cx;
		if (a.r == W_DOUBLE && a.d != W_DOUBLE)
			bot_end = cx;
		fill_rect(mask, w, h, 0, hy_top, top_end, hy_top + lt, 255);
		fill_rect(mask, w, h, 0, hy_bot, bot_end, hy_bot + lt, 255);
	}

	/* Right arm */
	if (a.r == W_LIGHT) {
		fill_rect(mask, w, h, v_left, ly0, w, ly1, 255);
	} else if (a.r == W_HEAVY) {
		fill_rect(mask, w, h, v_left, hy0, w, hy1, 255);
	} else if (a.r == W_DOUBLE) {
		int top_beg = (a.u == W_DOUBLE) ? vx2 :
			      (a.d == W_DOUBLE && a.l != W_DOUBLE) ? vx1 :
			      (a.u == W_LIGHT || a.d == W_LIGHT) ? lx0 : cx;
		int bot_beg = (a.d == W_DOUBLE) ? vx2 :
			      (a.u == W_DOUBLE && a.l != W_DOUBLE) ? vx1 :
			      (a.u == W_LIGHT || a.d == W_LIGHT) ? lx0 : cx;
		if (a.l == W_DOUBLE && a.u != W_DOUBLE)
			top_beg = cx;
		if (a.l == W_DOUBLE && a.d != W_DOUBLE)
			bot_beg = cx;
		fill_rect(mask, w, h, top_beg, hy_top, w, hy_top + lt, 255);
		fill_rect(mask, w, h, bot_beg, hy_bot, w, hy_bot + lt, 255);
	}

	/* Up arm */
	if (a.u == W_LIGHT) {
		fill_rect(mask, w, h, lx0, 0, lx1, h_bot, 255);
	} else if (a.u == W_HEAVY) {
		fill_rect(mask, w, h, hx0, 0, hx1, h_bot, 255);
	} else if (a.u == W_DOUBLE) {
		int left_end = (a.l == W_DOUBLE) ? hy_top + lt :
			       (a.r == W_DOUBLE && a.d != W_DOUBLE) ? hy_bot + lt :
			       (a.l == W_LIGHT || a.r == W_LIGHT) ? ly1 : cy;
		int right_end = (a.r == W_DOUBLE) ? hy_top + lt :
				(a.l == W_DOUBLE && a.d != W_DOUBLE) ? hy_bot + lt :
				(a.l == W_LIGHT || a.r == W_LIGHT) ? ly1 : cy;
		if (a.d == W_DOUBLE && a.l != W_DOUBLE)
			left_end = cy;
		if (a.d == W_DOUBLE && a.r != W_DOUBLE)
			right_end = cy;
		fill_rect(mask, w, h, vx1, 0, vx1 + lt, left_end, 255);
		fill_rect(mask, w, h, vx2, 0, vx2 + lt, right_end, 255);
	}

	/* Down arm */
	if (a.d == W_LIGHT) {
		fill_rect(mask, w, h, lx0, h_top, lx1, h, 255);
	} else if (a.d == W_HEAVY) {
		fill_rect(mask, w, h, hx0, h_top, hx1, h, 255);
	} else if (a.d == W_DOUBLE) {
		int left_beg = (a.l == W_DOUBLE) ? hy_bot :
			       (a.r == W_DOUBLE && a.u != W_DOUBLE) ? hy_top :
			       (a.l == W_LIGHT || a.r == W_LIGHT) ? ly0 : cy;
		int right_beg = (a.r == W_DOUBLE) ? hy_bot :
				(a.l == W_DOUBLE && a.u != W_DOUBLE) ? hy_top :
				(a.l == W_LIGHT || a.r == W_LIGHT) ? ly0 : cy;
		if (a.u == W_DOUBLE && a.l != W_DOUBLE)
			left_beg = cy;
		if (a.u == W_DOUBLE && a.r != W_DOUBLE)
			right_beg = cy;
		fill_rect(mask, w, h, vx1, left_beg, vx1 + lt, h, 255);
		fill_rect(mask, w, h, vx2, right_beg, vx2 + lt, h, 255);
	}
}

static void draw_dashed(uint8_t *mask, int w, int h, bool vert, bool heavy,
	int count)
{
	int lt = h / 14;
	if (lt < 1)
		lt = 1;
	int thick = heavy ? (lt * 2 < 2 ? 2 : lt * 2) : lt;
	if (!vert) {
		int y0 = h / 2 - thick / 2;
		int step = w / count;
		int dash = step > 1 ? step - 1 : 1;
		for (int i = 0; i < count; i++) {
			int x0 = (i * w) / count;
			fill_rect(mask, w, h, x0, y0, x0 + dash, y0 + thick, 255);
		}
	} else {
		int x0 = w / 2 - thick / 2;
		int step = h / count;
		int dash = step > 1 ? step - 1 : 1;
		for (int i = 0; i < count; i++) {
			int y0 = (i * h) / count;
			fill_rect(mask, w, h, x0, y0, x0 + thick, y0 + dash, 255);
		}
	}
}

static void draw_arc(uint8_t *mask, int w, int h, uint32_t cp)
{
	int lt = h / 14;
	if (lt < 1)
		lt = 1;
	float half = (float)lt * 0.5f;
	float cx = (float)(w / 2) + (float)(lt % 2) * 0.5f;
	float cy = (float)(h / 2) + (float)(lt % 2) * 0.5f;
	float rx = (float)w - cx;
	if (cx < rx)
		rx = cx;
	float ry = (float)h - cy;
	if (cy < ry)
		ry = cy;
	float r = (rx < ry ? rx : ry) - 0.5f;
	if (r < 1.5f) {
		/* Too tiny for an arc: draw a square corner. */
		uint8_t fallback = cp == 0x256D ? ARMS(0, 1, 0, 1) :
				   cp == 0x256E ? ARMS(1, 0, 0, 1) :
				   cp == 0x256F ? ARMS(1, 0, 1, 0) :
						  ARMS(0, 1, 1, 0);
		draw_arms(mask, w, h, fallback);
		return;
	}

	/* Arc center (ox, oy) and straight stubs to cell edges. */
	float ox, oy;
	int lx0 = w / 2 - lt / 2, lx1 = lx0 + lt;
	int ly0 = h / 2 - lt / 2, ly1 = ly0 + lt;
	if (cp == 0x256D) { /* ╭ down + right */
		ox = cx + r;
		oy = cy + r;
		fill_rect(mask, w, h, (int)floorf(ox), ly0, w, ly1, 255);
		fill_rect(mask, w, h, lx0, (int)floorf(oy), lx1, h, 255);
	} else if (cp == 0x256E) { /* ╮ down + left */
		ox = cx - r;
		oy = cy + r;
		fill_rect(mask, w, h, 0, ly0, (int)ceilf(ox), ly1, 255);
		fill_rect(mask, w, h, lx0, (int)floorf(oy), lx1, h, 255);
	} else if (cp == 0x256F) { /* ╯ up + left */
		ox = cx - r;
		oy = cy - r;
		fill_rect(mask, w, h, 0, ly0, (int)ceilf(ox), ly1, 255);
		fill_rect(mask, w, h, lx0, 0, lx1, (int)ceilf(oy), 255);
	} else { /* ╰ up + right */
		ox = cx + r;
		oy = cy - r;
		fill_rect(mask, w, h, (int)floorf(ox), ly0, w, ly1, 255);
		fill_rect(mask, w, h, lx0, 0, lx1, (int)ceilf(oy), 255);
	}

	for (int y = 0; y < h; y++) {
		float py = (float)y + 0.5f;
		bool in_y = (cp == 0x256D || cp == 0x256E) ?
			(py >= cy - half && py <= oy + 0.5f) :
			(py <= cy + half && py >= oy - 0.5f);
		if (!in_y)
			continue;
		for (int x = 0; x < w; x++) {
			float px = (float)x + 0.5f;
			bool in_x = (cp == 0x256D || cp == 0x2570) ?
				(px >= cx - half && px <= ox + 0.5f) :
				(px <= cx + half && px >= ox - 0.5f);
			if (!in_x)
				continue;
			float d = fabsf(hypotf(px - ox, py - oy) - r);
			float a = half + 0.5f - d;
			if (a <= 0.0f)
				continue;
			if (a > 1.0f)
				a = 1.0f;
			uint8_t cov = (uint8_t)(a * 255.0f + 0.5f);
			uint8_t *dst = &mask[(size_t)y * (size_t)w + (size_t)x];
			if (cov > *dst)
				*dst = cov;
		}
	}
}

static void draw_diagonal(uint8_t *mask, int w, int h, bool slash,
	bool backslash)
{
	int lt = h / 14;
	if (lt < 1)
		lt = 1;
	float half = (float)lt * 0.5f;
	float fw = (float)w, fh = (float)h;
	float norm = hypotf(fw, fh);
	if (norm <= 0.0f)
		return;
	for (int y = 0; y < h; y++) {
		float py = (float)y + 0.5f;
		for (int x = 0; x < w; x++) {
			float px = (float)x + 0.5f;
			float best = 0.0f;
			if (slash) {
				/* Line from (0, fh) to (fw, 0) */
				float d = fabsf(fh * px + fw * py - fw * fh) / norm;
				float a = half + 0.5f - d;
				if (a > best)
					best = a;
			}
			if (backslash) {
				/* Line from (0, 0) to (fw, fh) */
				float d = fabsf(fh * px - fw * py) / norm;
				float a = half + 0.5f - d;
				if (a > best)
					best = a;
			}
			if (best > 0.0f) {
				if (best > 1.0f)
					best = 1.0f;
				uint8_t cov = (uint8_t)(best * 255.0f + 0.5f);
				uint8_t *dst = &mask[(size_t)y * (size_t)w + (size_t)x];
				if (cov > *dst)
					*dst = cov;
			}
		}
	}
}

static void draw_block(uint8_t *mask, int w, int h, uint32_t cp)
{
	static const uint8_t quads[10] = {
		4,  /* 2596 ▖ LL */
		8,  /* 2597 ▗ LR */
		1,  /* 2598 ▘ UL */
		13, /* 2599 ▙ UL|LL|LR */
		9,  /* 259A ▚ UL|LR */
		7,  /* 259B ▛ UL|UR|LL */
		11, /* 259C ▜ UL|UR|LR */
		2,  /* 259D ▝ UR */
		6,  /* 259E ▞ UR|LL */
		14, /* 259F ▟ UR|LL|LR */
	};

	if (cp == 0x2580) {
		fill_rect(mask, w, h, 0, 0, w, h / 2, 255);
	} else if (cp >= 0x2581 && cp <= 0x2588) {
		int k = (int)(cp - 0x2580);
		int bh = (k == 8) ? h : (h * k + 4) / 8;
		if (bh < 1)
			bh = 1;
		fill_rect(mask, w, h, 0, h - bh, w, h, 255);
	} else if (cp >= 0x2589 && cp <= 0x258F) {
		int k = 8 - (int)(cp - 0x2588);
		int bw = (w * k + 4) / 8;
		if (bw < 1)
			bw = 1;
		fill_rect(mask, w, h, 0, 0, bw, h, 255);
	} else if (cp == 0x2590) {
		fill_rect(mask, w, h, w / 2, 0, w, h, 255);
	} else if (cp >= 0x2591 && cp <= 0x2593) {
		uint8_t cov = cp == 0x2591 ? 64 : cp == 0x2592 ? 128 : 192;
		fill_rect(mask, w, h, 0, 0, w, h, cov);
	} else if (cp == 0x2594) {
		int bh = (h + 4) / 8;
		if (bh < 1)
			bh = 1;
		fill_rect(mask, w, h, 0, 0, w, bh, 255);
	} else if (cp == 0x2595) {
		int bw = (w + 4) / 8;
		if (bw < 1)
			bw = 1;
		fill_rect(mask, w, h, w - bw, 0, w, h, 255);
	} else if (cp >= 0x2596 && cp <= 0x259F) {
		uint8_t q = quads[cp - 0x2596];
		int mx = w / 2, my = h / 2;
		if (q & 1)
			fill_rect(mask, w, h, 0, 0, mx, my, 255);
		if (q & 2)
			fill_rect(mask, w, h, mx, 0, w, my, 255);
		if (q & 4)
			fill_rect(mask, w, h, 0, my, mx, h, 255);
		if (q & 8)
			fill_rect(mask, w, h, mx, my, w, h, 255);
	}
}

static void draw_braille(uint8_t *mask, int w, int h, uint32_t cp)
{
	static const uint8_t bit_at[4][2] = {
		{ 0x01, 0x08 },
		{ 0x02, 0x10 },
		{ 0x04, 0x20 },
		{ 0x40, 0x80 },
	};
	uint8_t bits = (uint8_t)(cp - 0x2800);
	if (bits == 0)
		return;
	int pad_x = w / 8;
	int pad_y = h / 16;
	int cell_dx = w / 2;
	int cell_dy = h / 4;
	int dot_w = cell_dx - (pad_x > 0 ? pad_x : 1);
	int dot_h = cell_dy - (pad_y > 0 ? pad_y : 1);
	if (dot_w < 1)
		dot_w = 1;
	if (dot_h < 1)
		dot_h = 1;

	for (int r = 0; r < 4; r++) {
		int ry0 = (r * h) / 4;
		int ry1 = ((r + 1) * h) / 4;
		int y0 = ry0 + ((ry1 - ry0) - dot_h) / 2;
		for (int c = 0; c < 2; c++) {
			if (!(bits & bit_at[r][c]))
				continue;
			int cx0 = (c * w) / 2;
			int cx1 = ((c + 1) * w) / 2;
			int x0 = cx0 + ((cx1 - cx0) - dot_w) / 2;
			fill_rect(mask, w, h, x0, y0, x0 + dot_w, y0 + dot_h,
				255);
		}
	}
}

bool tl_box_draw(uint32_t cp, uint8_t *mask, int w, int h)
{
	if (w <= 0 || h <= 0)
		return false;
	if (cp >= 0x2500 && cp <= 0x257F) {
		memset(mask, 0, (size_t)w * (size_t)h);
		uint32_t off = cp - 0x2500;
		if (cp >= 0x2504 && cp <= 0x250B) {
			bool heavy = (off & 1u) != 0;
			bool vert = (off & 2u) != 0;
			int count = cp >= 0x2508 ? 4 : 3;
			draw_dashed(mask, w, h, vert, heavy, count);
		} else if (cp >= 0x254C && cp <= 0x254F) {
			bool heavy = (off & 1u) != 0;
			bool vert = (off & 2u) != 0;
			draw_dashed(mask, w, h, vert, heavy, 2);
		} else if (cp >= 0x256D && cp <= 0x2570) {
			draw_arc(mask, w, h, cp);
		} else if (cp >= 0x2571 && cp <= 0x2573) {
			draw_diagonal(mask, w, h, cp != 0x2572, cp != 0x2571);
		} else if (box_table[off] != 0) {
			draw_arms(mask, w, h, box_table[off]);
		}
		return true;
	}
	if (cp >= 0x2580 && cp <= 0x259F) {
		memset(mask, 0, (size_t)w * (size_t)h);
		draw_block(mask, w, h, cp);
		return true;
	}
	if (cp >= 0x2800 && cp <= 0x28FF) {
		memset(mask, 0, (size_t)w * (size_t)h);
		draw_braille(mask, w, h, cp);
		return true;
	}
	return false;
}
