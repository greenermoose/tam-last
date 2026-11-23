/* Keyboard URL mode: collect visible OSC 8 and plain-text URLs, assign
 * prefix-free hint labels, and validate URIs before launching. Pure logic. */
#ifndef TL_URL_H
#define TL_URL_H

#include <stdbool.h>
#include <stddef.h>

#include "tam-last/term.h"

#define TL_URL_MAX 256
#define TL_URL_LABEL_MAX 8

struct tl_url_item {
	int row; /* visible row 0..rows-1 */
	int x0;
	int x1; /* inclusive column index */
	char label[TL_URL_LABEL_MAX];
	char *uri;
};

struct tl_url_mode {
	bool active;
	struct tl_url_item items[TL_URL_MAX];
	size_t count;
	int label_len;
	char typed[TL_URL_LABEL_MAX];
	size_t typed_len;
};

enum tl_url_step {
	TL_URL_IGNORED = 0,
	TL_URL_PENDING,
	TL_URL_MATCHED
};

/* Validate that uri uses an allowlisted scheme and contains no control chars. */
bool tl_url_is_safe(const char *uri);

/* Scan visible rows of t (OSC 8 hyperlinks first, then plain-text URLs),
 * populate items with hint labels, and activate URL mode. Returns count. */
size_t tl_url_start(struct tl_url_mode *u, const struct tl_term *t);

/* Exit URL mode and free owned URI strings. */
void tl_url_end(struct tl_url_mode *u);

/* Feed one ASCII character (folded to lowercase). On TL_URL_MATCHED, sets
 * *out_uri to a pointer valid until tl_url_end(). */
enum tl_url_step tl_url_feed(struct tl_url_mode *u, char ch,
	const char **out_uri);

/* Remove the last typed hint character (Backspace). */
void tl_url_pop(struct tl_url_mode *u);

/* Format the bottom status bar text for URL mode. */
void tl_url_format_bar(const struct tl_url_mode *u, char *buf, size_t cap);

#endif
