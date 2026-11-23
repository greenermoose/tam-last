/* Incremental scrollback search: smart-case UTF-8 matching, wrap-around
 * navigation, and viewport synchronization. */
#include "tam-last/search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t utf8_encode_cp(uint32_t cp, char out[4])
{
	if (cp < 0x80u) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800u) {
		out[0] = (char)(0xc0u | (cp >> 6));
		out[1] = (char)(0x80u | (cp & 0x3fu));
		return 2;
	}
	if (cp < 0x10000u) {
		out[0] = (char)(0xe0u | (cp >> 12));
		out[1] = (char)(0x80u | ((cp >> 6) & 0x3fu));
		out[2] = (char)(0x80u | (cp & 0x3fu));
		return 3;
	}
	if (cp <= 0x10ffffu) {
		out[0] = (char)(0xf0u | (cp >> 18));
		out[1] = (char)(0x80u | ((cp >> 12) & 0x3fu));
		out[2] = (char)(0x80u | ((cp >> 6) & 0x3fu));
		out[3] = (char)(0x80u | (cp & 0x3fu));
		return 4;
	}
	return 0;
}

static uint32_t utf8_decode_next(const char *s, size_t len, size_t *pos)
{
	unsigned char b0 = (unsigned char)s[*pos];
	if (b0 < 0x80u) {
		(*pos)++;
		return b0;
	}
	uint32_t cp = 0;
	size_t need = 0;
	uint32_t min_cp = 0;
	if ((b0 & 0xe0u) == 0xc0u) {
		cp = b0 & 0x1fu; need = 2; min_cp = 0x80u;
	} else if ((b0 & 0xf0u) == 0xe0u) {
		cp = b0 & 0x0fu; need = 3; min_cp = 0x800u;
	} else if ((b0 & 0xf8u) == 0xf0u) {
		cp = b0 & 0x07u; need = 4; min_cp = 0x10000u;
	} else {
		(*pos)++;
		return 0xfffdu;
	}
	if (*pos + need > len) {
		(*pos)++;
		return 0xfffdu;
	}
	for (size_t i = 1; i < need; i++) {
		unsigned char bx = (unsigned char)s[*pos + i];
		if ((bx & 0xc0u) != 0x80u) {
			(*pos)++;
			return 0xfffdu;
		}
		cp = (cp << 6) | (bx & 0x3fu);
	}
	*pos += need;
	if (cp < min_cp || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu))
		return 0xfffdu;
	return cp;
}

static uint32_t fold_ascii(uint32_t cp, bool case_sensitive)
{
	if (!case_sensitive && cp >= 'A' && cp <= 'Z')
		return cp + ('a' - 'A');
	return cp;
}

static bool push_match(struct tl_search *s, int abs_y, int x0, int x1)
{
	if (s->match_count >= TL_SEARCH_MAX_MATCHES)
		return false;
	if (s->match_count == s->match_cap) {
		size_t ncap = s->match_cap == 0 ? 64 : s->match_cap * 2;
		if (ncap > TL_SEARCH_MAX_MATCHES)
			ncap = TL_SEARCH_MAX_MATCHES;
		struct tl_search_match *nm = realloc(s->matches,
			ncap * sizeof(*nm));
		if (nm == NULL)
			return false;
		s->matches = nm;
		s->match_cap = ncap;
	}
	s->matches[s->match_count++] = (struct tl_search_match){ abs_y, x0, x1 };
	return true;
}

void tl_search_start(struct tl_search *s, const struct tl_term *t)
{
	s->active = true;
	s->query[0] = '\0';
	s->query_len = 0;
	s->match_count = 0;
	s->current = -1;
	s->saved_view_offset = (t != NULL && !t->alt_screen) ? t->view_offset : 0;
}

void tl_search_end(struct tl_search *s)
{
	s->active = false;
	s->query[0] = '\0';
	s->query_len = 0;
	s->match_count = 0;
	s->current = -1;
}

void tl_search_free(struct tl_search *s)
{
	free(s->matches);
	memset(s, 0, sizeof(*s));
	s->current = -1;
}

void tl_search_refresh(struct tl_search *s, const struct tl_term *t)
{
	s->match_count = 0;
	s->current = -1;
	if (!s->active || t == NULL || s->query_len == 0)
		return;

	uint32_t qcp[TL_SEARCH_QUERY_MAX];
	size_t qlen = 0;
	bool case_sensitive = false;
	size_t pos = 0;
	while (pos < s->query_len && qlen < TL_SEARCH_QUERY_MAX) {
		uint32_t cp = utf8_decode_next(s->query, s->query_len, &pos);
		if (cp >= 'A' && cp <= 'Z')
			case_sensitive = true;
		qcp[qlen++] = cp;
	}
	if (qlen == 0)
		return;

	int total_rows = (t->alt_screen ? 0 : t->sb_len) + t->rows;
	for (int y = 0; y < total_rows; y++) {
		int ncols = 0;
		const struct tl_cell *row = tl_term_abs_row(t, y, &ncols);
		if (row == NULL || ncols <= 0)
			continue;
		for (int x = 0; x < ncols; x++) {
			if (row[x].cp == TL_CELL_WIDE_TAIL)
				continue;
			int cx = x;
			size_t qi = 0;
			int last_col = x;
			while (qi < qlen && cx < ncols) {
				if (row[cx].cp == TL_CELL_WIDE_TAIL) {
					cx++;
					continue;
				}
				uint32_t c = fold_ascii(row[cx].cp,
					case_sensitive);
				uint32_t q = fold_ascii(qcp[qi],
					case_sensitive);
				if (c != q)
					break;
				last_col = cx;
				if (cx + 1 < ncols &&
				    row[cx + 1].cp == TL_CELL_WIDE_TAIL) {
					last_col = cx + 1;
					cx += 2;
				} else {
					cx++;
				}
				qi++;
			}
			if (qi == qlen) {
				if (!push_match(s, y, x, last_col))
					goto done;
			}
		}
	}

done:
	if (s->match_count > 0) {
		/* Backward search: pick the last match at or above the bottom
		 * of the current viewport, else wrap to the newest match. */
		int sb = t->alt_screen ? 0 : t->sb_len;
		int vo = t->alt_screen ? 0 : t->view_offset;
		int view_bot = sb - vo + t->rows - 1;
		int best = (int)s->match_count - 1;
		for (size_t i = s->match_count; i > 0; i--) {
			if (s->matches[i - 1].abs_y <= view_bot) {
				best = (int)(i - 1);
				break;
			}
		}
		s->current = best;
	}
}

void tl_search_push_cp(struct tl_search *s, const struct tl_term *t,
	uint32_t cp)
{
	if (!s->active || cp < 0x20u || cp == 0x7fu)
		return;
	char u8[4];
	size_t n = utf8_encode_cp(cp, u8);
	if (n == 0 || s->query_len + n >= TL_SEARCH_QUERY_MAX)
		return;
	memcpy(s->query + s->query_len, u8, n);
	s->query_len += n;
	s->query[s->query_len] = '\0';
	tl_search_refresh(s, t);
}

void tl_search_push_utf8(struct tl_search *s, const struct tl_term *t,
	const char *utf8, size_t len)
{
	if (!s->active || utf8 == NULL || len == 0)
		return;
	size_t pos = 0;
	bool changed = false;
	while (pos < len) {
		uint32_t cp = utf8_decode_next(utf8, len, &pos);
		if (cp < 0x20u || cp == 0x7fu)
			continue;
		char u8[4];
		size_t n = utf8_encode_cp(cp, u8);
		if (n == 0 || s->query_len + n >= TL_SEARCH_QUERY_MAX)
			break;
		memcpy(s->query + s->query_len, u8, n);
		s->query_len += n;
		changed = true;
	}
	if (changed) {
		s->query[s->query_len] = '\0';
		tl_search_refresh(s, t);
	}
}

void tl_search_pop_cp(struct tl_search *s, const struct tl_term *t)
{
	if (!s->active || s->query_len == 0)
		return;
	size_t n = s->query_len - 1;
	while (n > 0 && ((unsigned char)s->query[n] & 0xc0u) == 0x80u)
		n--;
	s->query_len = n;
	s->query[n] = '\0';
	tl_search_refresh(s, t);
}

void tl_search_pop_word(struct tl_search *s, const struct tl_term *t)
{
	if (!s->active || s->query_len == 0)
		return;
	size_t n = s->query_len;
	while (n > 0 && s->query[n - 1] == ' ')
		n--;
	while (n > 0 && s->query[n - 1] != ' ')
		n--;
	s->query_len = n;
	s->query[n] = '\0';
	tl_search_refresh(s, t);
}

void tl_search_clear_query(struct tl_search *s, const struct tl_term *t)
{
	if (!s->active)
		return;
	s->query[0] = '\0';
	s->query_len = 0;
	tl_search_refresh(s, t);
}

void tl_search_next(struct tl_search *s, int delta)
{
	if (!s->active || s->match_count == 0 || delta == 0)
		return;
	int n = (int)s->match_count;
	if (s->current < 0)
		s->current = 0;
	int idx = (s->current + delta) % n;
	if (idx < 0)
		idx += n;
	s->current = idx;
}

void tl_search_sync_view(const struct tl_search *s, struct tl_term *t)
{
	const struct tl_search_match *m = tl_search_current(s);
	if (m == NULL || t == NULL || t->alt_screen)
		return;
	int view_top = t->sb_len - t->view_offset;
	int view_bot = view_top + t->rows - 1;
	if (m->abs_y >= view_top && m->abs_y <= view_bot)
		return;
	int want_top = m->abs_y - t->rows / 2;
	if (want_top < 0)
		want_top = 0;
	if (want_top > t->sb_len)
		want_top = t->sb_len;
	int want_off = t->sb_len - want_top;
	if (want_off != t->view_offset) {
		t->view_offset = want_off;
		tl_term_mark_all(t);
	}
}

const struct tl_search_match *tl_search_current(const struct tl_search *s)
{
	if (s == NULL || !s->active || s->current < 0 ||
	    (size_t)s->current >= s->match_count)
		return NULL;
	return &s->matches[s->current];
}

int tl_search_cell_state(const struct tl_search *s, int x, int abs_y)
{
	if (s == NULL || !s->active || s->match_count == 0)
		return 0;
	/* Binary search to first match with abs_y >= target. */
	size_t lo = 0, hi = s->match_count;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (s->matches[mid].abs_y < abs_y)
			lo = mid + 1;
		else
			hi = mid;
	}
	int state = 0;
	for (size_t i = lo; i < s->match_count && s->matches[i].abs_y == abs_y; i++) {
		if (x >= s->matches[i].x0 && x <= s->matches[i].x1) {
			if ((int)i == s->current)
				return 2;
			state = 1;
		}
	}
	return state;
}

void tl_search_format_bar(const struct tl_search *s, char *buf, size_t cap)
{
	if (buf == NULL || cap == 0)
		return;
	if (s == NULL || !s->active) {
		buf[0] = '\0';
		return;
	}
	if (s->query_len == 0) {
		snprintf(buf, cap, "search: _  (type to match, Ctrl+R/S prev/next, Enter select, Esc cancel)");
	} else if (s->match_count == 0) {
		snprintf(buf, cap, "search: %s_  [no match]", s->query);
	} else {
		snprintf(buf, cap, "search: %s_  [%d/%zu]", s->query,
			s->current + 1, s->match_count);
	}
}
