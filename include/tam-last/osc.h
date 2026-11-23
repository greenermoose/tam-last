/* OSC helpers: X11 color parsing and base64 encoding/decoding. */
#ifndef TL_OSC_H
#define TL_OSC_H

#include <stddef.h>
#include <stdint.h>

/* Parse an X11/xterm color spec (rgb:r/g/b with 1..4 hex digits per channel,
 * or #RGB, #RRGGBB, #RRRGGGBBB, #RRRRGGGGBBBB) into 24-bit 0xRRGGBB.
 * Returns 0 on success, -1 on invalid input. */
int tl_osc_parse_color(const char *s, size_t len, uint32_t *out);

/* Decode base64 (ignoring ASCII whitespace). Allocates *out (NUL-terminated,
 * *out_len bytes). Returns 0 on success, -1 on malformed input or OOM. */
int tl_base64_decode(const char *src, size_t len, char **out, size_t *out_len);

/* Encode binary bytes as base64. Allocates *out (NUL-terminated, *out_len
 * bytes). Returns 0 on success, -1 on OOM. */
int tl_base64_encode(const void *src, size_t len, char **out, size_t *out_len);

#endif
