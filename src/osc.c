/* Operating System Commands (OSC): window titles (0, 2), working directory (7),
 * hyperlinks (8), clipboard with policy (52), and dynamic palette/color queries
 * and changes (4, 10, 11, 12, 104, 110, 111, 112). */
#include "tam-last/osc.h"
#include "tam-last/term.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex_val(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Parse 1..4 hex digits and scale to an 8-bit channel (0..255). */
static int parse_hex_chan(const char *s, size_t n, uint32_t *out)
{
	if (n < 1 || n > 4)
		return -1;
	uint32_t v = 0;
	for (size_t i = 0; i < n; i++) {
		int d = hex_val((unsigned char)s[i]);
		if (d < 0)
			return -1;
		v = (v << 4) | (uint32_t)d;
	}
	switch (n) {
	case 1: *out = v * 0x11u; break;
	case 2: *out = v; break;
	case 3: *out = v >> 4; break;
	case 4: *out = v >> 8; break;
	default: return -1;
	}
	return 0;
}

int tl_osc_parse_color(const char *s, size_t len, uint32_t *out)
{
	if (s == NULL || out == NULL || len == 0)
		return -1;

	if (s[0] == '#') {
		size_t digits = len - 1;
		if (digits != 3 && digits != 6 && digits != 9 && digits != 12)
			return -1;
		size_t cw = digits / 3;
		uint32_t r = 0, g = 0, b = 0;
		if (parse_hex_chan(s + 1, cw, &r) != 0 ||
		    parse_hex_chan(s + 1 + cw, cw, &g) != 0 ||
		    parse_hex_chan(s + 1 + 2 * cw, cw, &b) != 0)
			return -1;
		*out = (r << 16) | (g << 8) | b;
		return 0;
	}

	if (len > 4 &&
	    (s[0] == 'r' || s[0] == 'R') &&
	    (s[1] == 'g' || s[1] == 'G') &&
	    (s[2] == 'b' || s[2] == 'B') &&
	    s[3] == ':') {
		const char *p = s + 4;
		const char *end = s + len;
		const char *s1 = memchr(p, '/', (size_t)(end - p));
		if (s1 == NULL)
			return -1;
		const char *s2 = memchr(s1 + 1, '/', (size_t)(end - (s1 + 1)));
		if (s2 == NULL)
			return -1;
		/* Reject any extra slash. */
		if (memchr(s2 + 1, '/', (size_t)(end - (s2 + 1))) != NULL)
			return -1;
		uint32_t r = 0, g = 0, b = 0;
		if (parse_hex_chan(p, (size_t)(s1 - p), &r) != 0 ||
		    parse_hex_chan(s1 + 1, (size_t)(s2 - (s1 + 1)), &g) != 0 ||
		    parse_hex_chan(s2 + 1, (size_t)(end - (s2 + 1)), &b) != 0)
			return -1;
		*out = (r << 16) | (g << 8) | b;
		return 0;
	}

	return -1;
}

static int b64_val(unsigned char c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return -1;
}

int tl_base64_decode(const char *src, size_t len, char **out, size_t *out_len)
{
	if (out == NULL || out_len == NULL)
		return -1;
	*out = NULL;
	*out_len = 0;
	if (src == NULL && len > 0)
		return -1;

	size_t cap = (len / 4) * 3 + 4;
	char *buf = malloc(cap);
	if (buf == NULL)
		return -1;

	uint32_t acc = 0;
	int count = 0;
	int pad = 0;
	size_t w = 0;

	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)src[i];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
			continue;
		if (c == '=') {
			pad++;
			if (pad > 2) {
				free(buf);
				return -1;
			}
			continue;
		}
		if (pad > 0) {
			/* Data after padding is malformed. */
			free(buf);
			return -1;
		}
		int v = b64_val(c);
		if (v < 0) {
			free(buf);
			return -1;
		}
		acc = (acc << 6) | (uint32_t)v;
		count++;
		if (count == 4) {
			buf[w++] = (char)((acc >> 16) & 0xff);
			buf[w++] = (char)((acc >> 8) & 0xff);
			buf[w++] = (char)(acc & 0xff);
			acc = 0;
			count = 0;
		}
	}

	if (count == 1) {
		free(buf);
		return -1;
	}
	if (count == 2) {
		if (pad != 0 && pad != 2) {
			free(buf);
			return -1;
		}
		buf[w++] = (char)((acc >> 4) & 0xff);
	} else if (count == 3) {
		if (pad != 0 && pad != 1) {
			free(buf);
			return -1;
		}
		buf[w++] = (char)((acc >> 10) & 0xff);
		buf[w++] = (char)((acc >> 2) & 0xff);
	} else if (pad != 0) {
		free(buf);
		return -1;
	}

	buf[w] = '\0';
	*out = buf;
	*out_len = w;
	return 0;
}

int tl_base64_encode(const void *src, size_t len, char **out, size_t *out_len)
{
	static const char tbl[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	if (out == NULL || out_len == NULL)
		return -1;
	*out = NULL;
	*out_len = 0;
	if (src == NULL && len > 0)
		return -1;

	size_t n = ((len + 2) / 3) * 4;
	char *buf = malloc(n + 1);
	if (buf == NULL)
		return -1;

	const unsigned char *p = src;
	size_t w = 0;
	for (size_t i = 0; i < len; i += 3) {
		uint32_t v = (uint32_t)p[i] << 16;
		size_t rem = len - i;
		if (rem > 1)
			v |= (uint32_t)p[i + 1] << 8;
		if (rem > 2)
			v |= (uint32_t)p[i + 2];
		buf[w++] = tbl[(v >> 18) & 0x3f];
		buf[w++] = tbl[(v >> 12) & 0x3f];
		buf[w++] = rem > 1 ? tbl[(v >> 6) & 0x3f] : '=';
		buf[w++] = rem > 2 ? tbl[v & 0x3f] : '=';
	}
	buf[w] = '\0';
	*out = buf;
	*out_len = w;
	return 0;
}

static void osc_title(struct tl_term *t, const char *s, size_t len)
{
	size_t w = 0;
	for (size_t i = 0; i < len && w + 1 < sizeof(t->title); i++) {
		unsigned char c = (unsigned char)s[i];
		if (c < 0x20 || c == 0x7f)
			continue;
		t->title[w++] = (char)c;
	}
	/* Avoid splitting a multi-byte UTF-8 sequence at the truncation edge. */
	if (w == sizeof(t->title) - 1 && len > w) {
		while (w > 0 && ((unsigned char)t->title[w - 1] & 0xc0) == 0x80)
			w--;
		if (w > 0 && (unsigned char)t->title[w - 1] >= 0xc0)
			w--;
	}
	t->title[w] = '\0';
	t->title_dirty = true;
}

static bool ascii_ieq(const char *a, const char *b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
			return false;
	}
	return true;
}

static void osc_cwd(struct tl_term *t, const char *s, size_t len)
{
	if (len == 0) {
		free(t->cwd);
		t->cwd = NULL;
		return;
	}
	if (len < 8 || !ascii_ieq(s, "file://", 7))
		return;

	const char *host = s + 7;
	const char *end = s + len;
	const char *slash = memchr(host, '/', (size_t)(end - host));
	if (slash == NULL)
		return;

	size_t hlen = (size_t)(slash - host);
	if (hlen > 0) {
		bool is_localhost = hlen == 9 && ascii_ieq(host, "localhost", 9);
		size_t my_hlen = strlen(t->hostname);
		bool is_local = my_hlen > 0 && hlen == my_hlen &&
			ascii_ieq(host, t->hostname, hlen);
		if (!is_localhost && !is_local)
			return;
	}

	const char *path = slash;
	size_t plen = (size_t)(end - path);
	if (plen == 0 || plen > TL_CWD_MAX * 3)
		return;

	char *decoded = malloc(plen + 1);
	if (decoded == NULL)
		return;

	size_t w = 0;
	for (size_t i = 0; i < plen; i++) {
		unsigned char c = (unsigned char)path[i];
		if (c == '%') {
			if (i + 2 >= plen) {
				free(decoded);
				return;
			}
			int hi = hex_val((unsigned char)path[i + 1]);
			int lo = hex_val((unsigned char)path[i + 2]);
			if (hi < 0 || lo < 0) {
				free(decoded);
				return;
			}
			unsigned char byte = (unsigned char)((hi << 4) | lo);
			if (byte == 0) {
				free(decoded);
				return;
			}
			c = byte;
			i += 2;
		}
		if (w >= TL_CWD_MAX) {
			free(decoded);
			return;
		}
		decoded[w++] = (char)c;
	}
	decoded[w] = '\0';
	free(t->cwd);
	t->cwd = decoded;
}

static char *dup_slice(const char *s, size_t len)
{
	char *p = malloc(len + 1);
	if (p == NULL)
		return NULL;
	memcpy(p, s, len);
	p[len] = '\0';
	return p;
}

static void osc_hyperlink(struct tl_term *t, const char *s, size_t len)
{
	const char *semi = memchr(s, ';', len);
	if (semi == NULL) {
		t->pen.link_id = 0;
		return;
	}
	const char *params = s;
	size_t params_len = (size_t)(semi - s);
	const char *uri = semi + 1;
	size_t uri_len = len - params_len - 1;

	if (uri_len == 0) {
		t->pen.link_id = 0;
		return;
	}
	if (uri_len > TL_LINK_URI_MAX)
		return;
	for (size_t i = 0; i < uri_len; i++) {
		unsigned char c = (unsigned char)uri[i];
		if (c < 0x20 || c == 0x7f)
			return;
	}

	/* Extract optional id=<val> from ':'-separated key=value pairs. */
	const char *uid = NULL;
	size_t uid_len = 0;
	size_t pos = 0;
	while (pos < params_len) {
		const char *colon = memchr(params + pos, ':', params_len - pos);
		size_t seg_len = colon != NULL ?
			(size_t)(colon - (params + pos)) : (params_len - pos);
		if (seg_len > 3 && memcmp(params + pos, "id=", 3) == 0) {
			uid = params + pos + 3;
			uid_len = seg_len - 3;
		}
		pos += seg_len + (colon != NULL ? 1 : 0);
	}

	/* Reuse existing entry if explicit id= and uri both match. */
	if (uid != NULL && uid_len > 0) {
		for (size_t i = 0; i < TL_LINK_MAX; i++) {
			struct tl_link *l = &t->links[i];
			if (l->id == 0 || l->user_id == NULL || l->uri == NULL)
				continue;
			if (strlen(l->user_id) == uid_len &&
			    memcmp(l->user_id, uid, uid_len) == 0 &&
			    strlen(l->uri) == uri_len &&
			    memcmp(l->uri, uri, uri_len) == 0) {
				t->pen.link_id = l->id;
				return;
			}
		}
	}

	char *uri_copy = dup_slice(uri, uri_len);
	if (uri_copy == NULL)
		return;
	char *uid_copy = NULL;
	if (uid != NULL && uid_len > 0) {
		uid_copy = dup_slice(uid, uid_len);
		if (uid_copy == NULL) {
			free(uri_copy);
			return;
		}
	}

	uint32_t slot = t->link_cursor % TL_LINK_MAX;
	t->link_cursor++;
	free(t->links[slot].uri);
	free(t->links[slot].user_id);

	if (t->next_link_id == 0)
		t->next_link_id = 1;
	uint32_t id = t->next_link_id++;
	if (t->next_link_id == 0)
		t->next_link_id = 1;

	t->links[slot].id = id;
	t->links[slot].uri = uri_copy;
	t->links[slot].user_id = uid_copy;
	t->pen.link_id = id;
}

static void reply_color(struct tl_term *t, int osc_num, int idx, uint32_t rgb,
	bool st)
{
	unsigned r = (rgb >> 16) & 0xff;
	unsigned g = (rgb >> 8) & 0xff;
	unsigned b = rgb & 0xff;
	const char *term = st ? "\033\\" : "\007";
	char buf[64];
	int n;
	if (idx >= 0)
		n = snprintf(buf, sizeof(buf),
			"\033]%d;%d;rgb:%02x%02x/%02x%02x/%02x%02x%s",
			osc_num, idx, r, r, g, g, b, b, term);
	else
		n = snprintf(buf, sizeof(buf),
			"\033]%d;rgb:%02x%02x/%02x%02x/%02x%02x%s",
			osc_num, r, r, g, g, b, b, term);
	if (n > 0 && (size_t)n < sizeof(buf))
		tl_term_reply(t, buf, (size_t)n);
}

static int parse_uint(const char *s, size_t len, int *out)
{
	if (len == 0 || len > 5)
		return -1;
	int v = 0;
	for (size_t i = 0; i < len; i++) {
		if (s[i] < '0' || s[i] > '9')
			return -1;
		v = v * 10 + (s[i] - '0');
	}
	*out = v;
	return 0;
}

static void osc_palette(struct tl_term *t, const char *s, size_t len, bool st)
{
	size_t pos = 0;
	bool changed = false;

	while (pos < len) {
		const char *s1 = memchr(s + pos, ';', len - pos);
		if (s1 == NULL)
			break;
		size_t idx_len = (size_t)(s1 - (s + pos));
		int idx = -1;
		int ok_idx = parse_uint(s + pos, idx_len, &idx);

		pos = (size_t)(s1 - s) + 1;
		const char *s2 = memchr(s + pos, ';', len - pos);
		size_t val_len = s2 != NULL ?
			(size_t)(s2 - (s + pos)) : (len - pos);
		const char *val = s + pos;
		pos += val_len + (s2 != NULL ? 1 : 0);

		if (ok_idx != 0 || idx < 0 || idx > 255)
			continue;
		if (val_len == 1 && val[0] == '?') {
			reply_color(t, 4, idx, t->pal.index[idx], st);
		} else {
			uint32_t rgb = 0;
			if (tl_osc_parse_color(val, val_len, &rgb) == 0) {
				t->pal.index[idx] = rgb;
				changed = true;
			}
		}
	}
	if (changed)
		tl_term_mark_all(t);
}

static void osc_dynamic_color(struct tl_term *t, int start_code, const char *s,
	size_t len, bool st)
{
	size_t pos = 0;
	int code = start_code;
	bool changed = false;

	while (pos <= len && code <= 12) {
		const char *semi = pos < len ?
			memchr(s + pos, ';', len - pos) : NULL;
		size_t vlen = semi != NULL ?
			(size_t)(semi - (s + pos)) : (len - pos);
		const char *val = s + pos;

		uint32_t *slot = code == 10 ? &t->pal.fg :
			code == 11 ? &t->pal.bg : &t->pal.cursor;

		if (vlen == 1 && val[0] == '?') {
			reply_color(t, code, -1, *slot, st);
		} else if (vlen > 0) {
			uint32_t rgb = 0;
			if (tl_osc_parse_color(val, vlen, &rgb) == 0) {
				*slot = rgb;
				changed = true;
			}
		}

		if (semi == NULL)
			break;
		pos = (size_t)(semi - s) + 1;
		code++;
	}
	if (changed)
		tl_term_mark_all(t);
}

static void osc_reset_palette(struct tl_term *t, const char *s, size_t len)
{
	if (len == 0) {
		memcpy(t->pal.index, t->default_pal.index, sizeof(t->pal.index));
		tl_term_mark_all(t);
		return;
	}
	size_t pos = 0;
	bool any = false;
	while (pos < len) {
		const char *semi = memchr(s + pos, ';', len - pos);
		size_t seg_len = semi != NULL ?
			(size_t)(semi - (s + pos)) : (len - pos);
		int idx = -1;
		if (parse_uint(s + pos, seg_len, &idx) == 0 &&
		    idx >= 0 && idx <= 255) {
			t->pal.index[idx] = t->default_pal.index[idx];
			any = true;
		}
		if (semi == NULL)
			break;
		pos = (size_t)(semi - s) + 1;
	}
	if (any)
		tl_term_mark_all(t);
}

static void osc_clipboard(struct tl_term *t, const char *s, size_t len, bool st)
{
	const char *semi = memchr(s, ';', len);
	if (semi == NULL)
		return;
	const char *targets = s;
	size_t tlen = (size_t)(semi - s);
	const char *payload = semi + 1;
	size_t plen = len - tlen - 1;

	bool want_clip = false, want_prim = false;
	if (tlen == 0) {
		want_clip = true;
	} else {
		for (size_t i = 0; i < tlen; i++) {
			if (targets[i] == 'c')
				want_clip = true;
			else if (targets[i] == 'p' || targets[i] == 's')
				want_prim = true;
		}
	}
	if (!want_clip && !want_prim)
		return;

	if (plen == 1 && payload[0] == '?') {
		if (t->osc52_policy != TL_OSC52_READ &&
		    t->osc52_policy != TL_OSC52_BOTH)
			return;
		t->osc52_read_pending = true;
		t->osc52_read_prim = !want_clip && want_prim;
		t->osc52_read_st = st;
		if (tlen == 0) {
			snprintf(t->osc52_read_target,
				sizeof(t->osc52_read_target), "c");
		} else {
			size_t copy = tlen < sizeof(t->osc52_read_target) - 1 ?
				tlen : sizeof(t->osc52_read_target) - 1;
			memcpy(t->osc52_read_target, targets, copy);
			t->osc52_read_target[copy] = '\0';
		}
		return;
	}

	if (t->osc52_policy != TL_OSC52_WRITE &&
	    t->osc52_policy != TL_OSC52_BOTH)
		return;

	char *decoded = NULL;
	size_t dlen = 0;
	if (tl_base64_decode(payload, plen, &decoded, &dlen) != 0)
		return;

	free(t->osc52_write_data);
	t->osc52_write_data = decoded;
	t->osc52_write_len = dlen;
	t->osc52_write_clip = want_clip;
	t->osc52_write_prim = want_prim;
	t->osc52_write_pending = true;
}

static size_t append_sanitized(char *dst, size_t cap, size_t cur,
	const char *src, size_t len, bool allow_nl)
{
	if (cap == 0)
		return 0;
	size_t w = cur;
	for (size_t i = 0; i < len && w + 1 < cap; i++) {
		unsigned char c = (unsigned char)src[i];
		if (c == '\r')
			continue;
		if (c == '\n') {
			if (allow_nl)
				dst[w++] = '\n';
			continue;
		}
		if (c < 0x20u || c == 0x7fu)
			continue;
		dst[w++] = (char)c;
	}
	/* Avoid splitting a multi-byte UTF-8 sequence at the truncation edge. */
	if (w == cap - 1 && len > 0) {
		size_t k = w;
		while (k > cur && ((unsigned char)dst[k - 1] & 0xc0u) == 0x80u)
			k--;
		if (k > cur && (unsigned char)dst[k - 1] >= 0xc0u) {
			unsigned char lead = (unsigned char)dst[k - 1];
			size_t need = (lead & 0xe0u) == 0xc0u ? 2 :
				(lead & 0xf0u) == 0xe0u ? 3 :
				(lead & 0xf8u) == 0xf0u ? 4 : 1;
			if (w - (k - 1) < need)
				w = k - 1;
		}
	}
	dst[w] = '\0';
	return w;
}

static void osc_notify_777(struct tl_term *t, const char *s, size_t len)
{
	/* Format: notify;<title>;<body> */
	const char *s1 = memchr(s, ';', len);
	if (s1 == NULL)
		return;
	size_t cmd_len = (size_t)(s1 - s);
	if (cmd_len != 6 || memcmp(s, "notify", 6) != 0)
		return;

	const char *rest = s1 + 1;
	size_t rlen = len - cmd_len - 1;
	const char *s2 = memchr(rest, ';', rlen);
	const char *title = rest;
	size_t tlen = s2 != NULL ? (size_t)(s2 - rest) : rlen;
	const char *body = s2 != NULL ? s2 + 1 : "";
	size_t blen = s2 != NULL ? rlen - tlen - 1 : 0;

	struct tl_notify n;
	memset(&n, 0, sizeof(n));
	n.urgency = TL_NOTIFY_NORMAL;
	(void)append_sanitized(n.title, sizeof(n.title), 0, title, tlen, false);
	(void)append_sanitized(n.body, sizeof(n.body), 0, body, blen, true);
	if (n.title[0] == '\0' && n.body[0] == '\0')
		return;
	n.pending = true;
	t->notify = n;
}

static void osc_notify_99(struct tl_term *t, const char *s, size_t len)
{
	/* Format: <meta>;<payload> where meta is ':'-separated k=v pairs. */
	const char *semi = memchr(s, ';', len);
	if (semi == NULL)
		return;
	const char *meta = s;
	size_t mlen = (size_t)(semi - s);
	const char *payload = semi + 1;
	size_t plen = len - mlen - 1;

	bool is_body = false;
	bool done = true;
	bool b64 = false;
	bool has_urg = false;
	enum tl_notify_urgency urg = TL_NOTIFY_NORMAL;
	bool has_occ = false;
	bool only_unfocused = false;
	const char *id = NULL;
	size_t id_len = 0;

	size_t pos = 0;
	while (pos < mlen) {
		const char *colon = memchr(meta + pos, ':', mlen - pos);
		size_t seg_len = colon != NULL ?
			(size_t)(colon - (meta + pos)) : (mlen - pos);
		const char *seg = meta + pos;
		const char *eq = memchr(seg, '=', seg_len);
		if (eq != NULL) {
			size_t klen = (size_t)(eq - seg);
			const char *val = eq + 1;
			size_t vlen = seg_len - klen - 1;
			if (klen == 1 && seg[0] == 'p') {
				if (vlen == 4 && memcmp(val, "body", 4) == 0)
					is_body = true;
				else if (vlen == 5 && memcmp(val, "title", 5) == 0)
					is_body = false;
			} else if (klen == 1 && seg[0] == 'd' && vlen == 1) {
				if (val[0] == '0')
					done = false;
				else if (val[0] == '1')
					done = true;
			} else if (klen == 1 && seg[0] == 'e' && vlen == 1) {
				if (val[0] == '1')
					b64 = true;
				else if (val[0] == '0')
					b64 = false;
			} else if (klen == 1 && seg[0] == 'u' && vlen == 1) {
				if (val[0] == '0') {
					urg = TL_NOTIFY_LOW;
					has_urg = true;
				} else if (val[0] == '1') {
					urg = TL_NOTIFY_NORMAL;
					has_urg = true;
				} else if (val[0] == '2') {
					urg = TL_NOTIFY_CRITICAL;
					has_urg = true;
				}
			} else if (klen == 1 && seg[0] == 'o') {
				if (vlen == 9 && memcmp(val, "unfocused", 9) == 0) {
					only_unfocused = true;
					has_occ = true;
				} else if (vlen == 6 && memcmp(val, "always", 6) == 0) {
					only_unfocused = false;
					has_occ = true;
				}
			} else if (klen == 1 && seg[0] == 'i' && vlen > 0) {
				id = val;
				id_len = vlen;
			}
		}
		pos += seg_len + (colon != NULL ? 1 : 0);
	}

	char *decoded = NULL;
	const char *text = payload;
	size_t text_len = plen;
	if (b64) {
		if (tl_base64_decode(payload, plen, &decoded, &text_len) != 0)
			return;
		text = decoded;
	}

	struct tl_notify_acc *acc = &t->notify_acc;
	bool same_id = acc->active && id != NULL &&
		id_len == strlen(acc->id) &&
		memcmp(acc->id, id, id_len) == 0;
	bool continue_anon = acc->active && id == NULL && acc->id[0] == '\0';
	if (!same_id && !continue_anon) {
		memset(acc, 0, sizeof(*acc));
		acc->active = true;
		acc->urgency = TL_NOTIFY_NORMAL;
		if (id != NULL) {
			size_t copy = id_len < sizeof(acc->id) - 1 ?
				id_len : sizeof(acc->id) - 1;
			memcpy(acc->id, id, copy);
			acc->id[copy] = '\0';
		}
	}
	if (has_urg)
		acc->urgency = urg;
	if (has_occ)
		acc->only_unfocused = only_unfocused;

	if (is_body) {
		acc->body_len = append_sanitized(acc->body, sizeof(acc->body),
			acc->body_len, text, text_len, true);
	} else {
		acc->title_len = append_sanitized(acc->title,
			sizeof(acc->title), acc->title_len, text, text_len,
			false);
	}
	free(decoded);

	if (done) {
		if (acc->title[0] != '\0' || acc->body[0] != '\0') {
			struct tl_notify n;
			memset(&n, 0, sizeof(n));
			n.pending = true;
			n.only_unfocused = acc->only_unfocused;
			n.urgency = acc->urgency;
			memcpy(n.title, acc->title, sizeof(n.title));
			memcpy(n.body, acc->body, sizeof(n.body));
			t->notify = n;
		}
		memset(acc, 0, sizeof(*acc));
	}
}

void tl_term_osc(struct tl_term *t, const char *data, size_t len, bool st)
{
	if (len == 0)
		return;

	const char *semi = memchr(data, ';', len);
	size_t code_len = semi != NULL ? (size_t)(semi - data) : len;
	int code = -1;
	if (parse_uint(data, code_len, &code) != 0)
		return;

	const char *rest = semi != NULL ? semi + 1 : "";
	size_t rest_len = semi != NULL ? len - code_len - 1 : 0;

	switch (code) {
	case 0:
	case 2:
		if (semi != NULL)
			osc_title(t, rest, rest_len);
		break;
	case 4:
		if (semi != NULL)
			osc_palette(t, rest, rest_len, st);
		break;
	case 7:
		if (semi != NULL)
			osc_cwd(t, rest, rest_len);
		break;
	case 8:
		if (semi != NULL)
			osc_hyperlink(t, rest, rest_len);
		break;
	case 10:
	case 11:
	case 12:
		if (semi != NULL)
			osc_dynamic_color(t, code, rest, rest_len, st);
		break;
	case 52:
		if (semi != NULL)
			osc_clipboard(t, rest, rest_len, st);
		break;
	case 99:
		if (semi != NULL)
			osc_notify_99(t, rest, rest_len);
		break;
	case 104:
		osc_reset_palette(t, rest, rest_len);
		break;
	case 110:
		t->pal.fg = t->default_pal.fg;
		tl_term_mark_all(t);
		break;
	case 111:
		t->pal.bg = t->default_pal.bg;
		tl_term_mark_all(t);
		break;
	case 112:
		t->pal.cursor = t->default_pal.cursor;
		tl_term_mark_all(t);
		break;
	case 777:
		if (semi != NULL)
			osc_notify_777(t, rest, rest_len);
		break;
	default:
		break;
	}
}
