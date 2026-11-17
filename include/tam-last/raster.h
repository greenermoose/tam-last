/* Antialiased scan conversion of outlines into 8-bit coverage masks. */
#ifndef TL_RASTER_H
#define TL_RASTER_H

#include <stdint.h>

#include "tam-last/ttf.h"

/* Draw o into mask (w x h bytes, zeroed by this call). A font-unit point
 * (x, y) lands at pixel (x * sx + ox, oy - y * sy): y grows downward, oy
 * is the baseline. Parts outside the mask are clipped. Returns 0, or -1
 * when out of memory. */
int tl_raster(const struct tl_outline *o, float sx, float sy, float ox,
	float oy, uint8_t *mask, int w, int h);

#endif
