/* Exact-area scan conversion.
 *
 * Each line segment adds, to every pixel it crosses, the signed area it
 * covers to the left of the pixel's right edge, and the remainder to the
 * next pixel. A running sum along each row then gives the winding-weighted
 * coverage of every pixel. This is the accumulation method of Raph
 * Levien's font-rs and of stb_truetype's version 2 rasterizer, written
 * fresh here. The sum runs over the whole buffer, so a contribution just
 * past the right edge lands in the next row's first cell and cancels
 * there; rows of closed contours always sum to zero. */
#include "tam-last/raster.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct acc {
	float *a;
	int w, h;
};

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static void line(struct acc *c, float x0, float y0, float x1, float y1)
{
	if (y0 == y1)
		return;
	float dir = 1.0f;
	if (y0 > y1) {
		float t;
		dir = -1.0f;
		t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
	}
	if (y1 <= 0 || y0 >= (float)c->h)
		return;
	float dxdy = (x1 - x0) / (y1 - y0);
	float x = x0;
	if (y0 < 0) {
		x -= y0 * dxdy;
		y0 = 0;
	}
	if (y1 > (float)c->h)
		y1 = (float)c->h;
	int ystart = (int)y0;
	int yend = (int)ceilf(y1);
	float wmax = (float)c->w;

	for (int y = ystart; y < yend; y++) {
		float top = (float)y > y0 ? (float)y : y0;
		float bot = (float)(y + 1) < y1 ? (float)(y + 1) : y1;
		float dy = bot - top;
		float xnext = x + dxdy * dy;
		float d = dy * dir;
		float xa = clampf(x < xnext ? x : xnext, 0, wmax);
		float xb = clampf(x < xnext ? xnext : x, 0, wmax);
		float *row = c->a + (size_t)y * (size_t)c->w;
		float fa = floorf(xa);
		int ia = (int)fa;
		int ib = (int)ceilf(xb);

		if (ib <= ia + 1) {
			/* Within one pixel: split at the segment's mean x. */
			float xm = 0.5f * (xa + xb) - fa;
			row[ia] += d - d * xm;
			row[ia + 1] += d * xm;
		} else {
			float s = 1.0f / (xb - xa);
			float fx0 = xa - fa;
			float a0 = 0.5f * s * (1 - fx0) * (1 - fx0);
			float fx1 = xb - (float)ib + 1;
			float am = 0.5f * s * fx1 * fx1;
			row[ia] += d * a0;
			if (ib == ia + 2) {
				row[ia + 1] += d * (1 - a0 - am);
			} else {
				float a1 = s * (1.5f - fx0);
				row[ia + 1] += d * (a1 - a0);
				for (int i = ia + 2; i < ib - 1; i++)
					row[i] += d * s;
				float a2 = a1 + (float)(ib - ia - 3) * s;
				row[ib - 1] += d * (1 - a2 - am);
			}
			row[ib] += d * am;
		}
		x = xnext;
	}
}

/* Flatten a quadratic curve into enough segments that it stays within
 * about a tenth of a pixel of the true curve. */
static void quad(struct acc *c, float x0, float y0, float cx, float cy,
	float x1, float y1)
{
	float ddx = x0 - 2 * cx + x1, ddy = y0 - 2 * cy + y1;
	float dd = sqrtf(ddx * ddx + ddy * ddy);
	int n = 1 + (int)sqrtf(dd * 2.5f);
	if (n > 32)
		n = 32;
	float px = x0, py = y0;
	for (int i = 1; i <= n; i++) {
		float t = (float)i / (float)n, u = 1 - t;
		float qx = u * u * x0 + 2 * u * t * cx + t * t * x1;
		float qy = u * u * y0 + 2 * u * t * cy + t * t * y1;
		line(c, px, py, qx, qy);
		px = qx;
		py = qy;
	}
}

int tl_raster(const struct tl_outline *o, float sx, float sy, float ox,
	float oy, uint8_t *mask, int w, int h)
{
	memset(mask, 0, (size_t)w * (size_t)h);
	if (o->npoints == 0 || w <= 0 || h <= 0)
		return 0;
	/* One spare cell per row end, plus slack for the last row. */
	struct acc c = { calloc((size_t)w * (size_t)h + 4, sizeof(float)), w, h };
	if (c.a == NULL)
		return -1;

	size_t start = 0;
	for (size_t k = 0; k < o->ncontours; k++) {
		size_t end = o->ends[k];
		size_t n = end + 1 - start;
		if (end < start || end >= o->npoints)
			break;
#define PX(i) (o->x[start + (i) % n] * sx + ox)
#define PY(i) (oy - o->y[start + (i) % n] * sy)
#define ON(i) (o->on[start + (i) % n])
		size_t first = 0;
		while (first < n && !ON(first))
			first++;
		if (first == n) {
			/* Only control points: every on-curve point is implied,
			 * halfway between two of them. */
			float px = 0.5f * (PX(n - 1) + PX(0));
			float py = 0.5f * (PY(n - 1) + PY(0));
			for (size_t i = 0; i < n; i++) {
				float mx = 0.5f * (PX(i) + PX(i + 1));
				float my = 0.5f * (PY(i) + PY(i + 1));
				quad(&c, px, py, PX(i), PY(i), mx, my);
				px = mx;
				py = my;
			}
		} else {
			/* Walk once round from an on-curve point back to it. */
			float px = PX(first), py = PY(first);
			float cx = 0, cy = 0;
			int have_ctrl = 0;
			for (size_t i = 1; i <= n; i++) {
				size_t j = first + i;
				float x = PX(j), y = PY(j);
				if (ON(j)) {
					if (have_ctrl)
						quad(&c, px, py, cx, cy, x, y);
					else
						line(&c, px, py, x, y);
					px = x;
					py = y;
					have_ctrl = 0;
				} else if (have_ctrl) {
					/* Two control points in a row imply an
					 * on-curve point halfway between them. */
					float mx = 0.5f * (cx + x);
					float my = 0.5f * (cy + y);
					quad(&c, px, py, cx, cy, mx, my);
					px = mx;
					py = my;
					cx = x;
					cy = y;
				} else {
					cx = x;
					cy = y;
					have_ctrl = 1;
				}
			}
		}
#undef PX
#undef PY
#undef ON
		start = end + 1;
	}

	float sum = 0;
	size_t total = (size_t)w * (size_t)h;
	for (size_t i = 0; i < total; i++) {
		sum += c.a[i];
		float v = fabsf(sum);
		mask[i] = (uint8_t)(v >= 1.0f ? 255 : (int)(v * 255.0f + 0.5f));
	}
	free(c.a);
	return 0;
}
