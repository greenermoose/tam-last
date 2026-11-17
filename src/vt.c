/* The DEC-compatible parser, after Paul Williams' state diagram
 * (https://vt100.net/emu/dec_ansi_parser), as foot's vt.c also follows
 * it. Written fresh; foot was read for how a UTF-8 terminal treats bytes
 * at and above 0x80 (always text, never C1 controls). */
#include "tam-last/vt.h"

#include <string.h>

#include <tam_uni.h>

void tl_vt_init(struct tl_vt *vt)
{
	memset(vt, 0, sizeof(*vt));
}

static void clear(struct tl_vt *vt)
{
	vt->nparams = 0;
	vt->have_param = false;
	vt->priv = 0;
	vt->inter = 0;
	vt->ninter = 0;
	vt->params[0] = 0;
	vt->sub[0] = 0;
}

static void collect(struct tl_vt *vt, unsigned char c)
{
	if (vt->ninter++ == 0)
		vt->inter = (char)c;
}

static void param(struct tl_vt *vt, unsigned char c)
{
	if (vt->nparams == 0)
		vt->nparams = 1;
	if (c == ';' || c == ':') {
		if (vt->nparams < TL_VT_PARAMS) {
			vt->params[vt->nparams] = 0;
			vt->sub[vt->nparams] = c == ':';
		}
		vt->nparams++;
		return;
	}
	int i = vt->nparams - 1;
	if (i < TL_VT_PARAMS) {
		int v = vt->params[i] * 10 + (c - '0');
		vt->params[i] = v > TL_VT_PARAM_MAX ? TL_VT_PARAM_MAX : v;
	}
}

static void csi_dispatch(struct tl_vt *vt, struct tl_term *t, unsigned char c)
{
	struct tl_csi csi = {
		vt->priv, vt->ninter > 1 ? 0 : vt->inter, (char)c,
		vt->nparams > TL_VT_PARAMS ? TL_VT_PARAMS : vt->nparams,
		vt->params, vt->sub,
	};
	if (vt->ninter <= 1)
		tl_term_csi(t, &csi);
}

static void flush_utf8(struct tl_vt *vt, struct tl_term *t)
{
	if (vt->ulen > 0) {
		tl_term_print(t, 0xFFFD);
		vt->ulen = 0;
	}
}

static void text(struct tl_vt *vt, struct tl_term *t, unsigned char c)
{
	if (vt->ulen > 0) {
		if ((c & 0xC0) == 0x80) {
			vt->utf8[vt->ulen++] = c;
			if (vt->ulen == vt->uneed) {
				uint32_t cp;
				tam_utf8_decode((const char *)vt->utf8,
					(size_t)vt->ulen, &cp);
				vt->ulen = 0;
				tl_term_print(t, cp);
			}
			return;
		}
		flush_utf8(vt, t); /* broken sequence; c starts afresh */
	}
	if (c < 0x80) {
		tl_term_print(t, c);
		return;
	}
	size_t need = tam_utf8_seq_len(c);
	if (need < 2) {
		tl_term_print(t, 0xFFFD);
		return;
	}
	vt->utf8[0] = c;
	vt->ulen = 1;
	vt->uneed = (int)need;
}

static void step(struct tl_vt *vt, struct tl_term *t, unsigned char c)
{
	/* Transitions from anywhere. */
	if (c == 0x18 || c == 0x1A) {
		flush_utf8(vt, t);
		vt->state = TL_VT_GROUND;
		return;
	}
	if (c == 0x1B) {
		flush_utf8(vt, t);
		clear(vt);
		vt->state = TL_VT_ESCAPE;
		return;
	}

	switch (vt->state) {
	case TL_VT_GROUND:
		if (c < 0x20) {
			flush_utf8(vt, t);
			tl_term_control(t, c);
		} else if (c != 0x7F) {
			text(vt, t, c);
		}
		return;

	case TL_VT_ESCAPE:
		if (c < 0x20) {
			tl_term_control(t, c);
		} else if (c <= 0x2F) {
			collect(vt, c);
			vt->state = TL_VT_ESCAPE_INTER;
		} else if (c == '[') {
			vt->state = TL_VT_CSI_ENTRY;
		} else if (c == ']' || c == 'P' || c == 'X' || c == '^' ||
			   c == '_') {
			vt->state = TL_VT_STRING;
		} else if (c != 0x7F) {
			vt->state = TL_VT_GROUND;
			tl_term_esc(t, 0, (char)c);
		}
		return;

	case TL_VT_ESCAPE_INTER:
		if (c < 0x20) {
			tl_term_control(t, c);
		} else if (c <= 0x2F) {
			collect(vt, c);
		} else if (c != 0x7F) {
			vt->state = TL_VT_GROUND;
			if (vt->ninter == 1)
				tl_term_esc(t, vt->inter, (char)c);
		}
		return;

	case TL_VT_CSI_ENTRY:
	case TL_VT_CSI_PARAM:
		if (c < 0x20) {
			tl_term_control(t, c);
		} else if ((c >= '0' && c <= '9') || c == ';' || c == ':') {
			param(vt, c);
			vt->state = TL_VT_CSI_PARAM;
		} else if (c >= 0x3C && c <= 0x3F) {
			if (vt->state == TL_VT_CSI_ENTRY) {
				vt->priv = (char)c;
				vt->state = TL_VT_CSI_PARAM;
			} else {
				vt->state = TL_VT_CSI_IGNORE;
			}
		} else if (c <= 0x2F) {
			collect(vt, c);
			vt->state = TL_VT_CSI_INTER;
		} else if (c >= 0x40 && c <= 0x7E) {
			vt->state = TL_VT_GROUND;
			csi_dispatch(vt, t, c);
		}
		return;

	case TL_VT_CSI_INTER:
		if (c < 0x20) {
			tl_term_control(t, c);
		} else if (c <= 0x2F) {
			collect(vt, c);
		} else if (c <= 0x3F) {
			vt->state = TL_VT_CSI_IGNORE;
		} else if (c <= 0x7E) {
			vt->state = TL_VT_GROUND;
			csi_dispatch(vt, t, c);
		}
		return;

	case TL_VT_CSI_IGNORE:
		if (c < 0x20)
			tl_term_control(t, c);
		else if (c >= 0x40 && c <= 0x7E)
			vt->state = TL_VT_GROUND;
		return;

	case TL_VT_STRING:
		/* Skipped. ESC (handled above) starts ST; BEL ends an OSC. */
		if (c == 0x07)
			vt->state = TL_VT_GROUND;
		return;
	}
}

void tl_vt_feed(struct tl_vt *vt, struct tl_term *t, const unsigned char *s,
	size_t len)
{
	for (size_t i = 0; i < len; i++)
		step(vt, t, s[i]);
}
