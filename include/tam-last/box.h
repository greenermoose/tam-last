/* Procedural box-drawing (U+2500..U+257F), block elements (U+2580..U+259F),
 * and Braille patterns (U+2800..U+28FF) rendered directly into cell coverage
 * masks so lines and blocks join pixel-for-pixel across adjacent cells. */
#ifndef TL_BOX_H
#define TL_BOX_H

#include <stdbool.h>
#include <stdint.h>

/* Draw cp into a w x h coverage mask (0..255). Returns true if cp is in a
 * procedurally drawn range, false if it should be rendered from a font. */
bool tl_box_draw(uint32_t cp, uint8_t *mask, int w, int h);

#endif
