/* INI configuration loader using libtam's tam_ini. */
#include "tam-last/config.h"
#include "tam-last/keys.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tam.h>

#define INCLUDE_MAX_DEPTH 4

struct load_ctx {
	struct tl_config *cfg;
	int depth;
};

static int parse_hex_color(const char *s, uint32_t *out)
{
	if (s == NULL)
		return -1;
	if (s[0] == '#')
		s++;
	if (strlen(s) != 6)
		return -1;
	uint32_t v = 0;
	for (int i = 0; i < 6; i++) {
		unsigned char c = (unsigned char)s[i];
		uint32_t d;
		if (c >= '0' && c <= '9')
			d = (uint32_t)(c - '0');
		else if (c >= 'a' && c <= 'f')
			d = (uint32_t)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			d = (uint32_t)(c - 'A' + 10);
		else
			return -1;
		v = (v << 4) | d;
	}
	*out = v;
	return 0;
}

static void expand_tilde(const char *src, char *dst, size_t dstlen)
{
	if (src[0] == '~' && (src[1] == '/' || src[1] == '\0')) {
		const char *home = getenv("HOME");
		if (home != NULL && home[0] != '\0') {
			snprintf(dst, dstlen, "%s%s", home, src + 1);
			return;
		}
	}
	snprintf(dst, dstlen, "%s", src);
}

int tl_osc52_policy_parse(const char *s, enum tl_osc52_policy *out)
{
	if (s == NULL || out == NULL)
		return -1;
	if (strcmp(s, "write") == 0 || strcmp(s, "copy") == 0) {
		*out = TL_OSC52_WRITE;
		return 0;
	}
	if (strcmp(s, "disabled") == 0 || strcmp(s, "none") == 0) {
		*out = TL_OSC52_DISABLED;
		return 0;
	}
	if (strcmp(s, "read") == 0 || strcmp(s, "paste") == 0) {
		*out = TL_OSC52_READ;
		return 0;
	}
	if (strcmp(s, "both") == 0 || strcmp(s, "copy-paste") == 0) {
		*out = TL_OSC52_BOTH;
		return 0;
	}
	return -1;
}

static int load_file_depth(struct tl_config *cfg, const char *path, int depth);

static int on_ini(void *user, const char *section, const char *name,
	const char *value)
{
	struct load_ctx *ctx = user;
	struct tl_config *cfg = ctx->cfg;

	if (section[0] == '\0' || strcmp(section, "main") == 0) {
		if (strcmp(name, "font") == 0) {
			expand_tilde(value, cfg->font, sizeof(cfg->font));
		} else if (strcmp(name, "font-bold") == 0) {
			expand_tilde(value, cfg->font_bold,
				sizeof(cfg->font_bold));
		} else if (strcmp(name, "font-italic") == 0) {
			expand_tilde(value, cfg->font_italic,
				sizeof(cfg->font_italic));
		} else if (strcmp(name, "font-bold-italic") == 0) {
			expand_tilde(value, cfg->font_bold_italic,
				sizeof(cfg->font_bold_italic));
		} else if (strcmp(name, "font-fallback") == 0) {
			expand_tilde(value, cfg->font_fallback,
				sizeof(cfg->font_fallback));
		} else if (strcmp(name, "layout") == 0) {
			if (tl_layout_find(value) != NULL)
				snprintf(cfg->layout, sizeof(cfg->layout),
					"%s", value);
			else
				fprintf(stderr, "tam-last: config: unknown layout: %s\n",
					value);
		} else if (strcmp(name, "term") == 0) {
			bool ok = value[0] != '\0';
			for (size_t i = 0; ok && value[i] != '\0'; i++) {
				unsigned char ch = (unsigned char)value[i];
				if (ch <= 0x20 || ch >= 0x7f || ch == '/')
					ok = false;
			}
			if (ok && strlen(value) < sizeof(cfg->term))
				snprintf(cfg->term, sizeof(cfg->term), "%s",
					value);
			else
				fprintf(stderr, "tam-last: config: bad term: %s\n",
					value);
		} else if (strcmp(name, "size") == 0) {
			char *end = NULL;
			float v = strtof(value, &end);
			if (end != value && *end == '\0' && v >= 3.0f &&
			    v <= 200.0f)
				cfg->size = v;
			else
				fprintf(stderr, "tam-last: config: bad size: %s\n",
					value);
		} else if (strcmp(name, "pad") == 0) {
			char *end = NULL;
			long v = strtol(value, &end, 10);
			if (end != value && *end == '\0' && v >= 0 && v <= 64)
				cfg->pad = (int)v;
			else
				fprintf(stderr, "tam-last: config: bad pad: %s\n",
					value);
		} else if (strcmp(name, "osc52") == 0) {
			enum tl_osc52_policy pol;
			if (tl_osc52_policy_parse(value, &pol) == 0)
				cfg->osc52 = pol;
			else
				fprintf(stderr, "tam-last: config: bad osc52 policy: %s\n",
					value);
		} else if (strcmp(name, "include") == 0) {
			if (ctx->depth < INCLUDE_MAX_DEPTH) {
				char inc[512];
				expand_tilde(value, inc, sizeof(inc));
				(void)load_file_depth(cfg, inc, ctx->depth + 1);
			}
		}
		return 0;
	}

	if (strcmp(section, "colors") == 0 ||
	    strcmp(section, "colors-dark") == 0) {
		uint32_t c = 0;
		if (strcmp(name, "foreground") == 0) {
			if (parse_hex_color(value, &c) == 0)
				cfg->pal.fg = c;
			else
				fprintf(stderr, "tam-last: config: bad color %s=%s\n",
					name, value);
		} else if (strcmp(name, "background") == 0) {
			if (parse_hex_color(value, &c) == 0)
				cfg->pal.bg = c;
			else
				fprintf(stderr, "tam-last: config: bad color %s=%s\n",
					name, value);
		} else if (strcmp(name, "cursor-text") == 0) {
			if (parse_hex_color(value, &c) == 0)
				cfg->pal.cursor_text = c;
			else
				fprintf(stderr, "tam-last: config: bad color %s=%s\n",
					name, value);
		} else if (strcmp(name, "cursor") == 0) {
			/* Either "RRGGBB" (cursor bg) or "TEXT BG" (foot.ini style). */
			const char *sp = strchr(value, ' ');
			if (sp == NULL)
				sp = strchr(value, '\t');
			if (sp == NULL) {
				if (parse_hex_color(value, &c) == 0)
					cfg->pal.cursor = c;
				else
					fprintf(stderr, "tam-last: config: bad cursor=%s\n",
						value);
			} else {
				char first[16];
				size_t flen = (size_t)(sp - value);
				if (flen < sizeof(first)) {
					memcpy(first, value, flen);
					first[flen] = '\0';
					while (*sp == ' ' || *sp == '\t')
						sp++;
					uint32_t c_text = 0, c_bg = 0;
					if (parse_hex_color(first, &c_text) == 0 &&
					    parse_hex_color(sp, &c_bg) == 0) {
						cfg->pal.cursor_text = c_text;
						cfg->pal.cursor = c_bg;
					} else {
						fprintf(stderr,
							"tam-last: config: bad cursor=%s\n",
							value);
					}
				}
			}
		} else if (strncmp(name, "regular", 7) == 0 &&
			   name[7] >= '0' && name[7] <= '7' && name[8] == '\0') {
			if (parse_hex_color(value, &c) == 0)
				cfg->pal.index[name[7] - '0'] = c;
		} else if (strncmp(name, "bright", 6) == 0 &&
			   name[6] >= '0' && name[6] <= '7' && name[7] == '\0') {
			if (parse_hex_color(value, &c) == 0)
				cfg->pal.index[8 + (name[6] - '0')] = c;
		} else if (strncmp(name, "color", 5) == 0) {
			char *end = NULL;
			long idx = strtol(name + 5, &end, 10);
			if (end != name + 5 && *end == '\0' &&
			    idx >= 0 && idx < 16 &&
			    parse_hex_color(value, &c) == 0)
				cfg->pal.index[idx] = c;
		}
		return 0;
	}
	return 0;
}

void tl_config_default(struct tl_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	snprintf(cfg->layout, sizeof(cfg->layout), "%s", "us");
	cfg->size = 9.0f;
	cfg->pad = 2;
	cfg->osc52 = TL_OSC52_WRITE;
	tl_palette_default(&cfg->pal);
}

void tl_config_default_path(char *buf, size_t len)
{
	if (len == 0)
		return;
	buf[0] = '\0';
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg != NULL && xdg[0] == '/') {
		snprintf(buf, len, "%s/tam-last/tam-last.ini", xdg);
		return;
	}
	const char *home = getenv("HOME");
	if (home != NULL && home[0] != '\0')
		snprintf(buf, len, "%s/.config/tam-last/tam-last.ini", home);
}

static int load_file_depth(struct tl_config *cfg, const char *path, int depth)
{
	struct load_ctx ctx = { cfg, depth };
	int r = tam_ini_parse_file(path, on_ini, &ctx);
	return r < 0 ? -1 : 0;
}

int tl_config_load_file(struct tl_config *cfg, const char *path)
{
	if (path == NULL || path[0] == '\0')
		return -1;
	return load_file_depth(cfg, path, 0);
}

int tl_config_load_string(struct tl_config *cfg, const char *text)
{
	if (text == NULL)
		return -1;
	struct load_ctx ctx = { cfg, 0 };
	return tam_ini_parse_string(text, on_ini, &ctx) == 0 ? 0 : -1;
}
