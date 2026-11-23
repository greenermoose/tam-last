/* The byte-stream parser for what programs write to the terminal: Paul
 * Williams' DEC-compatible state machine (https://vt100.net/emu/), with
 * UTF-8 text. It knows nothing about the screen; it hands each complete
 * piece to the terminal (term.c). */
#ifndef TL_VT_H
#define TL_VT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TL_VT_PARAMS 32
#define TL_VT_PARAM_MAX 65535
#define TL_VT_OSC_MAX (128u * 1024u)

enum tl_vt_state {
	TL_VT_GROUND,
	TL_VT_ESCAPE,
	TL_VT_ESCAPE_INTER,
	TL_VT_CSI_ENTRY,
	TL_VT_CSI_PARAM,
	TL_VT_CSI_INTER,
	TL_VT_CSI_IGNORE,
	TL_VT_OSC,
	TL_VT_OSC_ESC,
	TL_VT_STRING /* DCS, SOS, PM, APC: skipped until ST or BEL */
};

struct tl_vt {
	enum tl_vt_state state;
	int params[TL_VT_PARAMS];
	uint8_t sub[TL_VT_PARAMS]; /* 1 when the parameter followed a ':' */
	int nparams;
	bool have_param;
	char priv;  /* CSI private marker: < = > ? or 0 */
	char inter; /* the first intermediate byte, or 0 */
	int ninter;
	unsigned char utf8[4];
	int ulen, uneed;
	char *osc_buf;
	size_t osc_len;
	size_t osc_cap;
	bool osc_overflow;
};

/* A complete CSI sequence. Missing parameters are 0; the handler applies
 * its defaults. */
struct tl_csi {
	char priv, inter, final;
	int n;
	const int *p;
	const uint8_t *sub;
};

struct tl_term;

void tl_vt_init(struct tl_vt *vt);
void tl_vt_free(struct tl_vt *vt);
void tl_vt_feed(struct tl_vt *vt, struct tl_term *t, const unsigned char *s,
	size_t len);

/* Implemented by term.c and osc.c. */
void tl_term_print(struct tl_term *t, uint32_t cp);
void tl_term_control(struct tl_term *t, unsigned char c);
void tl_term_esc(struct tl_term *t, char inter, char final);
void tl_term_csi(struct tl_term *t, const struct tl_csi *csi);
void tl_term_osc(struct tl_term *t, const char *data, size_t len, bool st);

#endif
