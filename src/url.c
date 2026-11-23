/* Visible URL detection (OSC 8 + plain-text schemes) and hint-label matching. */
#include "tam-last/url.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char URL_ALPHABET[] = "sadfjklewcmpgh";

static const char *const URL_SCHEMES[] = {
	"https://",
	"http://",
	"ftp://",
	"ftps://",
	"file://",
	"mailto:",
};

static bool ascii_iprefix(const char *s, const char *prefix)
{
	for (size_t i = 0; prefix[i] != '\0'; i++) {
		if (tolower((unsigned char)s[i]) !=
		    tolower((unsigned char)prefix[i]))
			return false;
	}
	return true;
}

bool tl_url_is_safe(const char *uri)
{
	if (uri == NULL || uri[0] == '\0')
		return false;
	size_t len = strlen(uri);
	if (len > TL_LINK_URI_MAX)
		return false;
	bool scheme_ok = false;
	size_t scheme_len = 0;
	for (size_t i = 0; i < sizeof(URL_SCHEMES) / sizeof(URL_SCHEMES[0]); i++) {
		size_t sl = strlen(URL_SCHEMES[i]);
		if (len > sl && ascii_iprefix(uri, URL_SCHEMES[i])) {
			scheme_ok = true;
			scheme_len = sl;
			break;
		}
	}
	if (!scheme_ok || len <= scheme_len)
		return false;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)uri[i];
		if (c <= 0x20u || c == 0x7fu)
			return false;
	}
	return true;
}

static bool is_url_stop(uint32_t cp)
{
	if (cp <= 0x20u || cp >= 0x7fu)
		return true;
	switch (cp) {
	case '<': case '>': case '"': case '\'': case '`':
		return true;
	default:
		return false;
	}
}

static bool is_trailing_punct(char c)
{
	switch (c) {
	case '.': case ',': case ';': case ':': case '!': case '?':
		return true;
	default:
		return false;
	}
}

/* Trim trailing sentence punctuation and unmatched closing delimiters while
 * preserving balanced pairs like https://en.wikipedia.org/wiki/Foo_(bar). */
static size_t trim_url_end(const char *s, size_t len)
{
	while (len > 0) {
		char last = s[len - 1];
		if (is_trailing_punct(last)) {
			len--;
			continue;
		}
		char open = 0;
		if (last == ')')
			open = '(';
		else if (last == ']')
			open = '[';
		else if (last == '}')
			open = '{';
		if (open != 0) {
			int opens = 0, closes = 0;
			for (size_t i = 0; i < len; i++) {
				if (s[i] == open)
					opens++;
				else if (s[i] == last)
					closes++;
			}
			if (closes > opens) {
				len--;
				continue;
			}
		}
		break;
	}
	return len;
}

static bool add_item(struct tl_url_mode *u, int row, int x0, int x1,
	const char *uri, size_t uri_len)
{
	if (u->count >= TL_URL_MAX || uri_len == 0 || uri_len > TL_LINK_URI_MAX)
		return false;
	char *copy = malloc(uri_len + 1);
	if (copy == NULL)
		return false;
	memcpy(copy, uri, uri_len);
	copy[uri_len] = '\0';
	if (!tl_url_is_safe(copy)) {
		free(copy);
		return false;
	}
	struct tl_url_item *it = &u->items[u->count++];
	it->row = row;
	it->x0 = x0;
	it->x1 = x1;
	it->label[0] = '\0';
	it->uri = copy;
	return true;
}

static void assign_labels(struct tl_url_mode *u)
{
	size_t base = sizeof(URL_ALPHABET) - 1;
	int width = 1;
	size_t cap = base;
	while (cap < u->count && width < TL_URL_LABEL_MAX - 1) {
		cap *= base;
		width++;
	}
	u->label_len = width;
	for (size_t i = 0; i < u->count; i++) {
		size_t v = i;
		for (int d = width - 1; d >= 0; d--) {
			u->items[i].label[d] = URL_ALPHABET[v % base];
			v /= base;
		}
		u->items[i].label[width] = '\0';
	}
}

static size_t match_scheme_at(const struct tl_cell *row, int ncols, int x)
{
	for (size_t i = 0; i < sizeof(URL_SCHEMES) / sizeof(URL_SCHEMES[0]); i++) {
		const char *sc = URL_SCHEMES[i];
		size_t sl = strlen(sc);
		if (x + (int)sl >= ncols)
			continue;
		bool ok = true;
		for (size_t k = 0; k < sl; k++) {
			uint32_t cp = row[x + (int)k].cp;
			if (row[x + (int)k].link_id != 0 || cp >= 0x80u ||
			    tolower((unsigned char)cp) !=
			    tolower((unsigned char)sc[k])) {
				ok = false;
				break;
			}
		}
		if (ok)
			return sl;
	}
	return 0;
}

void tl_url_end(struct tl_url_mode *u)
{
	if (u == NULL)
		return;
	for (size_t i = 0; i < u->count; i++) {
		free(u->items[i].uri);
		u->items[i].uri = NULL;
	}
	u->count = 0;
	u->label_len = 0;
	u->typed[0] = '\0';
	u->typed_len = 0;
	u->active = false;
}

size_t tl_url_start(struct tl_url_mode *u, const struct tl_term *t)
{
	tl_url_end(u);
	if (t == NULL)
		return 0;

	for (int y = 0; y < t->rows && u->count < TL_URL_MAX; y++) {
		int ncols = 0;
		const struct tl_cell *row = tl_term_view_row(t, y, &ncols);
		if (row == NULL || ncols <= 0)
			continue;
		int lim = ncols < t->cols ? ncols : t->cols;
		int x = 0;
		while (x < lim && u->count < TL_URL_MAX) {
			/* 1. OSC 8 hyperlink span. */
			if (row[x].link_id != 0) {
				uint32_t lid = row[x].link_id;
				int x0 = x;
				while (x + 1 < lim && row[x + 1].link_id == lid)
					x++;
				const struct tl_link *lk = tl_term_link(t, lid);
				if (lk != NULL && lk->uri != NULL)
					(void)add_item(u, y, x0, x, lk->uri,
						strlen(lk->uri));
				x++;
				continue;
			}
			/* 2. Plain-text URL. */
			size_t sl = match_scheme_at(row, lim, x);
			if (sl > 0) {
				char buf[TL_LINK_URI_MAX + 1];
				size_t blen = 0;
				int x0 = x;
				int cur = x;
				while (cur < lim && row[cur].link_id == 0 &&
				       !is_url_stop(row[cur].cp) &&
				       blen < TL_LINK_URI_MAX) {
					buf[blen++] = (char)row[cur].cp;
					cur++;
				}
				size_t trimmed = trim_url_end(buf, blen);
				if (trimmed > sl) {
					(void)add_item(u, y, x0,
						x0 + (int)trimmed - 1,
						buf, trimmed);
					x = x0 + (int)trimmed;
					continue;
				}
			}
			x++;
		}
	}

	if (u->count > 0) {
		assign_labels(u);
		u->active = true;
	}
	return u->count;
}

enum tl_url_step tl_url_feed(struct tl_url_mode *u, char ch,
	const char **out_uri)
{
	if (out_uri != NULL)
		*out_uri = NULL;
	if (u == NULL || !u->active || u->count == 0)
		return TL_URL_IGNORED;
	char lc = (char)tolower((unsigned char)ch);
	if (u->typed_len + 1 >= TL_URL_LABEL_MAX)
		return TL_URL_IGNORED;

	char next[TL_URL_LABEL_MAX];
	memcpy(next, u->typed, u->typed_len);
	next[u->typed_len] = lc;
	size_t nlen = u->typed_len + 1;
	next[nlen] = '\0';

	bool any_prefix = false;
	for (size_t i = 0; i < u->count; i++) {
		if (strncmp(u->items[i].label, next, nlen) == 0) {
			any_prefix = true;
			if (u->items[i].label[nlen] == '\0') {
				if (out_uri != NULL)
					*out_uri = u->items[i].uri;
				return TL_URL_MATCHED;
			}
		}
	}
	if (!any_prefix)
		return TL_URL_IGNORED;
	memcpy(u->typed, next, nlen + 1);
	u->typed_len = nlen;
	return TL_URL_PENDING;
}

void tl_url_pop(struct tl_url_mode *u)
{
	if (u == NULL || !u->active || u->typed_len == 0)
		return;
	u->typed[--u->typed_len] = '\0';
}

void tl_url_format_bar(const struct tl_url_mode *u, char *buf, size_t cap)
{
	if (buf == NULL || cap == 0)
		return;
	if (u == NULL || !u->active) {
		buf[0] = '\0';
		return;
	}
	if (u->typed_len > 0) {
		snprintf(buf, cap, "url: %s_  (%zu links; Shift+key copies, Esc cancels)",
			u->typed, u->count);
	} else {
		snprintf(buf, cap, "url: type label to open  (%zu links; Shift+key copies, Esc cancels)",
			u->count);
	}
}
