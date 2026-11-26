/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Build-time compiler for tam-last keyboard layouts and compose rules.
 *
 * Usage:
 *   gen-layouts -o OUT.c -c layouts/compose layouts/us.layout [...]
 *
 * Zero dependencies beyond C17 libc. Validates evdev names, UTF-8 tokens,
 * and compose prefix conflicts at build time. */
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tam-last/evdev.h"
#include "tam-last/keys.h"

#define MAX_LAYOUTS 32
#define MAX_RULES   1024
#define MAX_LINE    512

struct key_entry {
	const char *name;
	uint32_t code;
};

static const struct key_entry evdev_table[] = {
	{ "KEY_ESC", TL_KEY_ESC },
	{ "KEY_1", TL_KEY_1 }, { "KEY_2", TL_KEY_2 }, { "KEY_3", TL_KEY_3 },
	{ "KEY_4", TL_KEY_4 }, { "KEY_5", TL_KEY_5 }, { "KEY_6", TL_KEY_6 },
	{ "KEY_7", TL_KEY_7 }, { "KEY_8", TL_KEY_8 }, { "KEY_9", TL_KEY_9 },
	{ "KEY_0", TL_KEY_0 },
	{ "KEY_MINUS", TL_KEY_MINUS }, { "KEY_EQUAL", TL_KEY_EQUAL },
	{ "KEY_BACKSPACE", TL_KEY_BACKSPACE }, { "KEY_TAB", TL_KEY_TAB },
	{ "KEY_Q", TL_KEY_Q }, { "KEY_W", TL_KEY_W }, { "KEY_E", TL_KEY_E },
	{ "KEY_R", TL_KEY_R }, { "KEY_T", TL_KEY_T }, { "KEY_Y", TL_KEY_Y },
	{ "KEY_U", TL_KEY_U }, { "KEY_I", TL_KEY_I }, { "KEY_O", TL_KEY_O },
	{ "KEY_P", TL_KEY_P },
	{ "KEY_LEFTBRACE", TL_KEY_LEFTBRACE },
	{ "KEY_RIGHTBRACE", TL_KEY_RIGHTBRACE },
	{ "KEY_ENTER", TL_KEY_ENTER }, { "KEY_LEFTCTRL", TL_KEY_LEFTCTRL },
	{ "KEY_A", TL_KEY_A }, { "KEY_S", TL_KEY_S }, { "KEY_D", TL_KEY_D },
	{ "KEY_F", TL_KEY_F }, { "KEY_G", TL_KEY_G }, { "KEY_H", TL_KEY_H },
	{ "KEY_J", TL_KEY_J }, { "KEY_K", TL_KEY_K }, { "KEY_L", TL_KEY_L },
	{ "KEY_SEMICOLON", TL_KEY_SEMICOLON },
	{ "KEY_APOSTROPHE", TL_KEY_APOSTROPHE },
	{ "KEY_GRAVE", TL_KEY_GRAVE },
	{ "KEY_LEFTSHIFT", TL_KEY_LEFTSHIFT },
	{ "KEY_BACKSLASH", TL_KEY_BACKSLASH },
	{ "KEY_Z", TL_KEY_Z }, { "KEY_X", TL_KEY_X }, { "KEY_C", TL_KEY_C },
	{ "KEY_V", TL_KEY_V }, { "KEY_B", TL_KEY_B }, { "KEY_N", TL_KEY_N },
	{ "KEY_M", TL_KEY_M },
	{ "KEY_COMMA", TL_KEY_COMMA }, { "KEY_DOT", TL_KEY_DOT },
	{ "KEY_SLASH", TL_KEY_SLASH },
	{ "KEY_RIGHTSHIFT", TL_KEY_RIGHTSHIFT },
	{ "KEY_KPASTERISK", TL_KEY_KPASTERISK },
	{ "KEY_LEFTALT", TL_KEY_LEFTALT }, { "KEY_SPACE", TL_KEY_SPACE },
	{ "KEY_CAPSLOCK", TL_KEY_CAPSLOCK },
	{ "KEY_F1", TL_KEY_F1 }, { "KEY_F2", TL_KEY_F2 },
	{ "KEY_F3", TL_KEY_F3 }, { "KEY_F4", TL_KEY_F4 },
	{ "KEY_F5", TL_KEY_F5 }, { "KEY_F6", TL_KEY_F6 },
	{ "KEY_F7", TL_KEY_F7 }, { "KEY_F8", TL_KEY_F8 },
	{ "KEY_F9", TL_KEY_F9 }, { "KEY_F10", TL_KEY_F10 },
	{ "KEY_NUMLOCK", TL_KEY_NUMLOCK },
	{ "KEY_SCROLLLOCK", TL_KEY_SCROLLLOCK },
	{ "KEY_KP7", TL_KEY_KP7 }, { "KEY_KP8", TL_KEY_KP8 },
	{ "KEY_KP9", TL_KEY_KP9 }, { "KEY_KPMINUS", TL_KEY_KPMINUS },
	{ "KEY_KP4", TL_KEY_KP4 }, { "KEY_KP5", TL_KEY_KP5 },
	{ "KEY_KP6", TL_KEY_KP6 }, { "KEY_KPPLUS", TL_KEY_KPPLUS },
	{ "KEY_KP1", TL_KEY_KP1 }, { "KEY_KP2", TL_KEY_KP2 },
	{ "KEY_KP3", TL_KEY_KP3 }, { "KEY_KP0", TL_KEY_KP0 },
	{ "KEY_KPDOT", TL_KEY_KPDOT },
	{ "KEY_102ND", TL_KEY_102ND },
	{ "KEY_F11", TL_KEY_F11 }, { "KEY_F12", TL_KEY_F12 },
	{ "KEY_KPENTER", TL_KEY_KPENTER },
	{ "KEY_RIGHTCTRL", TL_KEY_RIGHTCTRL },
	{ "KEY_KPSLASH", TL_KEY_KPSLASH },
	{ "KEY_SYSRQ", TL_KEY_SYSRQ }, { "KEY_RIGHTALT", TL_KEY_RIGHTALT },
	{ "KEY_HOME", TL_KEY_HOME }, { "KEY_UP", TL_KEY_UP },
	{ "KEY_PAGEUP", TL_KEY_PAGEUP }, { "KEY_LEFT", TL_KEY_LEFT },
	{ "KEY_RIGHT", TL_KEY_RIGHT }, { "KEY_END", TL_KEY_END },
	{ "KEY_DOWN", TL_KEY_DOWN }, { "KEY_PAGEDOWN", TL_KEY_PAGEDOWN },
	{ "KEY_INSERT", TL_KEY_INSERT }, { "KEY_DELETE", TL_KEY_DELETE },
	{ "KEY_LEFTMETA", TL_KEY_LEFTMETA },
	{ "KEY_RIGHTMETA", TL_KEY_RIGHTMETA },
	{ "KEY_COMPOSE", TL_KEY_COMPOSE },
};

static const struct key_entry dead_table[] = {
	{ "dead_acute", TL_KEY_DEAD_BIT | TL_DEAD_ACUTE },
	{ "dead_grave", TL_KEY_DEAD_BIT | TL_DEAD_GRAVE },
	{ "dead_circumflex", TL_KEY_DEAD_BIT | TL_DEAD_CIRCUMFLEX },
	{ "dead_tilde", TL_KEY_DEAD_BIT | TL_DEAD_TILDE },
	{ "dead_diaeresis", TL_KEY_DEAD_BIT | TL_DEAD_DIAERESIS },
	{ "dead_cedilla", TL_KEY_DEAD_BIT | TL_DEAD_CEDILLA },
	{ "dead_ring", TL_KEY_DEAD_BIT | TL_DEAD_RING },
};

struct parsed_layout {
	char name[64];
	bool has_altgr;
	uint32_t map[TL_KEY_COUNT][4];
};

static struct parsed_layout layouts[MAX_LAYOUTS];
static size_t layout_count = 0;

static struct tl_compose_rule rules[MAX_RULES];
static size_t rule_count = 0;

static int lookup_evdev(const char *name, uint32_t *out)
{
	for (size_t i = 0; i < sizeof(evdev_table) / sizeof(evdev_table[0]); i++) {
		if (strcmp(evdev_table[i].name, name) == 0) {
			*out = evdev_table[i].code;
			return 0;
		}
	}
	return -1;
}

static int decode_utf8_single(const char *tok, uint32_t *out)
{
	const unsigned char *s = (const unsigned char *)tok;
	size_t len = strlen(tok);
	if (len == 0)
		return -1;
	uint32_t cp = 0;
	size_t want = 0;
	if (s[0] < 0x80) {
		cp = s[0];
		want = 1;
	} else if ((s[0] & 0xe0) == 0xc0) {
		if (len < 2 || (s[1] & 0xc0) != 0x80)
			return -1;
		cp = ((uint32_t)(s[0] & 0x1f) << 6) | (uint32_t)(s[1] & 0x3f);
		if (cp < 0x80)
			return -1;
		want = 2;
	} else if ((s[0] & 0xf0) == 0xe0) {
		if (len < 3 || (s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80)
			return -1;
		cp = ((uint32_t)(s[0] & 0x0f) << 12) |
		     ((uint32_t)(s[1] & 0x3f) << 6) |
		     (uint32_t)(s[2] & 0x3f);
		if (cp < 0x800 || (cp >= 0xd800 && cp <= 0xdfff))
			return -1;
		want = 3;
	} else if ((s[0] & 0xf8) == 0xf0) {
		if (len < 4 || (s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80 ||
		    (s[3] & 0xc0) != 0x80)
			return -1;
		cp = ((uint32_t)(s[0] & 0x07) << 18) |
		     ((uint32_t)(s[1] & 0x3f) << 12) |
		     ((uint32_t)(s[2] & 0x3f) << 6) |
		     (uint32_t)(s[3] & 0x3f);
		if (cp < 0x10000 || cp > 0x10ffff)
			return -1;
		want = 4;
	} else {
		return -1;
	}
	if (len != want)
		return -1;
	*out = cp;
	return 0;
}

static int parse_token(const char *tok, bool allow_compose, uint32_t *out)
{
	if (strcmp(tok, "none") == 0) {
		*out = 0;
		return 0;
	}
	if (strcmp(tok, "space") == 0) {
		*out = 0x20;
		return 0;
	}
	if (allow_compose && strcmp(tok, "compose") == 0) {
		*out = TL_COMPOSE_START;
		return 0;
	}
	for (size_t i = 0; i < sizeof(dead_table) / sizeof(dead_table[0]); i++) {
		if (strcmp(dead_table[i].name, tok) == 0) {
			*out = dead_table[i].code;
			return 0;
		}
	}
	if (tok[0] == 'U' && tok[1] == '+' && tok[2] != '\0') {
		char *end = NULL;
		unsigned long v = strtoul(tok + 2, &end, 16);
		if (*end != '\0' || v == 0 || v > 0x10ffff ||
		    (v >= 0xd800 && v <= 0xdfff))
			return -1;
		*out = (uint32_t)v;
		return 0;
	}
	return decode_utf8_single(tok, out);
}

static int split_tokens(char *line, char *toks[], int max_toks)
{
	int n = 0;
	char *p = line;
	while (*p != '\0') {
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			p++;
		if (*p == '\0')
			break;
		if (n == 0 && *p == '#')
			return 0;
		if (n >= max_toks)
			return -1;
		toks[n++] = p;
		while (*p != '\0' && *p != ' ' && *p != '\t' &&
		       *p != '\r' && *p != '\n')
			p++;
		if (*p != '\0')
			*p++ = '\0';
	}
	return n;
}

static int parse_layout_file(const char *path)
{
	if (layout_count >= MAX_LAYOUTS) {
		fprintf(stderr, "gen-layouts: too many layouts (max %d)\n",
			MAX_LAYOUTS);
		return -1;
	}
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		perror(path);
		return -1;
	}
	struct parsed_layout *l = &layouts[layout_count];
	memset(l, 0, sizeof(*l));

	char line[MAX_LINE];
	int lineno = 0;
	while (fgets(line, sizeof(line), f) != NULL) {
		lineno++;
		char *toks[8];
		int n = split_tokens(line, toks, 8);
		if (n < 0) {
			fprintf(stderr, "%s:%d: too many tokens\n", path, lineno);
			fclose(f);
			return -1;
		}
		if (n == 0)
			continue;

		if (strcmp(toks[0], "name") == 0) {
			if (n != 2 || strlen(toks[1]) >= sizeof(l->name)) {
				fprintf(stderr, "%s:%d: bad name directive\n",
					path, lineno);
				fclose(f);
				return -1;
			}
			for (size_t i = 0; i < layout_count; i++) {
				if (strcmp(layouts[i].name, toks[1]) == 0) {
					fprintf(stderr,
						"%s:%d: duplicate layout '%s'\n",
						path, lineno, toks[1]);
					fclose(f);
					return -1;
				}
			}
			strcpy(l->name, toks[1]);
		} else if (strcmp(toks[0], "altgr") == 0) {
			if (n != 2) {
				fprintf(stderr, "%s:%d: bad altgr directive\n",
					path, lineno);
				fclose(f);
				return -1;
			}
			if (strcmp(toks[1], "rightalt") == 0) {
				l->has_altgr = true;
			} else if (strcmp(toks[1], "none") == 0) {
				l->has_altgr = false;
			} else {
				fprintf(stderr,
					"%s:%d: altgr must be none or rightalt\n",
					path, lineno);
				fclose(f);
				return -1;
			}
		} else if (strcmp(toks[0], "include") == 0) {
			if (n != 2) {
				fprintf(stderr, "%s:%d: bad include directive\n",
					path, lineno);
				fclose(f);
				return -1;
			}
			const struct parsed_layout *base = NULL;
			for (size_t i = 0; i < layout_count; i++) {
				if (strcmp(layouts[i].name, toks[1]) == 0) {
					base = &layouts[i];
					break;
				}
			}
			if (base == NULL) {
				fprintf(stderr,
					"%s:%d: unknown include layout '%s'\n",
					path, lineno, toks[1]);
				fclose(f);
				return -1;
			}
			memcpy(l->map, base->map, sizeof(l->map));
		} else if (strncmp(toks[0], "KEY_", 4) == 0) {
			uint32_t code = 0;
			if (lookup_evdev(toks[0], &code) != 0) {
				fprintf(stderr, "%s:%d: unknown key '%s'\n",
					path, lineno, toks[0]);
				fclose(f);
				return -1;
			}
			if (n < 3 || n > 5) {
				fprintf(stderr,
					"%s:%d: key line needs 2..4 levels\n",
					path, lineno);
				fclose(f);
				return -1;
			}
			uint32_t lvls[4] = { 0, 0, 0, 0 };
			for (int i = 1; i < n; i++) {
				if (parse_token(toks[i], false, &lvls[i - 1]) != 0) {
					fprintf(stderr,
						"%s:%d: invalid token '%s'\n",
						path, lineno, toks[i]);
					fclose(f);
					return -1;
				}
			}
			for (int i = 0; i < 4; i++)
				l->map[code][i] = lvls[i];
		} else {
			fprintf(stderr, "%s:%d: unknown directive '%s'\n",
				path, lineno, toks[0]);
			fclose(f);
			return -1;
		}
	}
	fclose(f);
	if (l->name[0] == '\0') {
		fprintf(stderr, "%s: missing 'name' directive\n", path);
		return -1;
	}
	layout_count++;
	return 0;
}

static int parse_compose_file(const char *path)
{
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		perror(path);
		return -1;
	}
	char line[MAX_LINE];
	int lineno = 0;
	while (fgets(line, sizeof(line), f) != NULL) {
		lineno++;
		char *toks[10];
		int n = split_tokens(line, toks, 10);
		if (n < 0) {
			fprintf(stderr, "%s:%d: too many tokens\n", path, lineno);
			fclose(f);
			return -1;
		}
		if (n == 0)
			continue;
		int arrow = -1;
		for (int i = 0; i < n; i++) {
			if (strcmp(toks[i], "->") == 0) {
				arrow = i;
				break;
			}
		}
		if (arrow < 2 || arrow > TL_COMPOSE_MAX || arrow != n - 2) {
			fprintf(stderr,
				"%s:%d: expected 2..%d symbols, '->', and 1 result\n",
				path, lineno, TL_COMPOSE_MAX);
			fclose(f);
			return -1;
		}
		if (rule_count >= MAX_RULES) {
			fprintf(stderr, "%s:%d: too many compose rules (max %d)\n",
				path, lineno, MAX_RULES);
			fclose(f);
			return -1;
		}
		struct tl_compose_rule r;
		memset(&r, 0, sizeof(r));
		r.len = (uint8_t)arrow;
		for (int i = 0; i < arrow; i++) {
			if (parse_token(toks[i], i == 0, &r.seq[i]) != 0 ||
			    r.seq[i] == 0) {
				fprintf(stderr,
					"%s:%d: invalid sequence token '%s'\n",
					path, lineno, toks[i]);
				fclose(f);
				return -1;
			}
		}
		if (parse_token(toks[arrow + 1], false, &r.result) != 0 ||
		    r.result == 0 || (r.result & TL_KEY_DEAD_BIT) != 0) {
			fprintf(stderr, "%s:%d: invalid result token '%s'\n",
				path, lineno, toks[arrow + 1]);
			fclose(f);
			return -1;
		}
		rules[rule_count++] = r;
	}
	fclose(f);
	return 0;
}

static int cmp_rules(const void *pa, const void *pb)
{
	const struct tl_compose_rule *a = pa;
	const struct tl_compose_rule *b = pb;
	uint8_t m = a->len < b->len ? a->len : b->len;
	for (uint8_t i = 0; i < m; i++) {
		if (a->seq[i] < b->seq[i])
			return -1;
		if (a->seq[i] > b->seq[i])
			return 1;
	}
	if (a->len < b->len)
		return -1;
	if (a->len > b->len)
		return 1;
	return 0;
}

static int validate_compose_rules(const char *path)
{
	qsort(rules, rule_count, sizeof(rules[0]), cmp_rules);
	for (size_t i = 0; i + 1 < rule_count; i++) {
		const struct tl_compose_rule *a = &rules[i];
		const struct tl_compose_rule *b = &rules[i + 1];
		uint8_t m = a->len < b->len ? a->len : b->len;
		bool same = true;
		for (uint8_t j = 0; j < m; j++) {
			if (a->seq[j] != b->seq[j]) {
				same = false;
				break;
			}
		}
		if (same) {
			fprintf(stderr,
				"%s: duplicate or prefix-conflicting compose rule "
				"(first=0x%08x second=0x%08x)\n",
				path, a->seq[0], a->seq[1]);
			return -1;
		}
	}
	return 0;
}

static int emit_c(const char *out_path)
{
	FILE *f = fopen(out_path, "w");
	if (f == NULL) {
		perror(out_path);
		return -1;
	}
	fputs("/* Generated by tools/gen-layouts.c; do not edit by hand. */\n"
	      "#include \"tam-last/keys.h\"\n\n", f);

	fputs("const struct tl_layout tl_layouts[] = {\n", f);
	for (size_t i = 0; i < layout_count; i++) {
		const struct parsed_layout *l = &layouts[i];
		fprintf(f, "\t{\n\t\t.name = \"%s\",\n\t\t.has_altgr = %s,\n"
			"\t\t.map = {\n", l->name,
			l->has_altgr ? "true" : "false");
		for (uint32_t c = 0; c < TL_KEY_COUNT; c++) {
			if (l->map[c][0] == 0 && l->map[c][1] == 0 &&
			    l->map[c][2] == 0 && l->map[c][3] == 0)
				continue;
			fprintf(f, "\t\t\t[%u] = { 0x%xu, 0x%xu, 0x%xu, 0x%xu },\n",
				c, l->map[c][0], l->map[c][1],
				l->map[c][2], l->map[c][3]);
		}
		fputs("\t\t},\n\t},\n", f);
	}
	fprintf(f, "};\nconst size_t tl_layout_count_val = %zu;\n\n",
		layout_count);

	fputs("const struct tl_compose_rule tl_compose_rules[] = {\n", f);
	for (size_t i = 0; i < rule_count; i++) {
		const struct tl_compose_rule *r = &rules[i];
		fprintf(f, "\t{ %u, { 0x%xu, 0x%xu, 0x%xu, 0x%xu }, 0x%xu },\n",
			(unsigned)r->len, r->seq[0], r->seq[1],
			r->seq[2], r->seq[3], r->result);
	}
	fprintf(f, "};\nconst size_t tl_compose_rule_count_val = %zu;\n",
		rule_count);

	if (fclose(f) != 0) {
		perror(out_path);
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *out_path = NULL;
	const char *compose_path = NULL;
	const char *layout_paths[MAX_LAYOUTS];
	size_t npaths = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			out_path = argv[++i];
		} else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
			compose_path = argv[++i];
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "gen-layouts: unknown option %s\n",
				argv[i]);
			return 2;
		} else {
			if (npaths >= MAX_LAYOUTS) {
				fprintf(stderr, "gen-layouts: too many inputs\n");
				return 2;
			}
			layout_paths[npaths++] = argv[i];
		}
	}
	if (out_path == NULL || compose_path == NULL || npaths == 0) {
		fprintf(stderr,
			"usage: gen-layouts -o OUT.c -c COMPOSE LAYOUT...\n");
		return 2;
	}

	for (size_t i = 0; i < npaths; i++) {
		if (parse_layout_file(layout_paths[i]) != 0)
			return 1;
	}
	if (parse_compose_file(compose_path) != 0)
		return 1;
	if (validate_compose_rules(compose_path) != 0)
		return 1;
	if (emit_c(out_path) != 0)
		return 1;
	return 0;
}
