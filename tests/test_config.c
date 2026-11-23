/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Configuration file parsing via libtam's tam_ini. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "tam-last/config.h"

int main(int argc, char **argv)
{
	const char *outdir = argc > 1 ? argv[1] : ".";
	struct tl_config cfg;

	tl_config_default(&cfg);
	CHECK(strcmp(cfg.layout, "us") == 0);
	CHECK(cfg.size == 9.0f);
	CHECK(cfg.pad == 2);
	CHECK(cfg.pal.fg == 0xa9b1d6);
	CHECK(cfg.pal.bg == 0x1a1b26);
	CHECK(cfg.pal.cursor == 0xc0caf5);
	CHECK(cfg.pal.cursor_text == 0x1a1b26);
	CHECK(cfg.pal.index[1] == 0xf7768e);

	const char *ini =
		"[main]\n"
		"font = /tmp/custom-Regular.ttf\n"
		"font-bold = /tmp/custom-Bold.ttf\n"
		"font-italic = /tmp/custom-Italic.ttf\n"
		"font-bold-italic = /tmp/custom-BoldItalic.ttf\n"
		"font-fallback = /tmp/fallback.ttf\n"
		"layout = us-intl\n"
		"size = 11.5\n"
		"pad = 8\n"
		"\n"
		"[colors]\n"
		"foreground = #ffffff\n"
		"background = 000000\n"
		"cursor = 112233 aabbcc\n"
		"regular1 = ff0000\n"
		"bright2 = #00ff00\n"
		"color4 = 0000ff\n"
		"color15 = fedcba\n";
	CHECK(tl_config_load_string(&cfg, ini) == 0);
	CHECK(strcmp(cfg.font, "/tmp/custom-Regular.ttf") == 0);
	CHECK(strcmp(cfg.font_bold, "/tmp/custom-Bold.ttf") == 0);
	CHECK(strcmp(cfg.font_italic, "/tmp/custom-Italic.ttf") == 0);
	CHECK(strcmp(cfg.font_bold_italic, "/tmp/custom-BoldItalic.ttf") == 0);
	CHECK(strcmp(cfg.font_fallback, "/tmp/fallback.ttf") == 0);
	CHECK(strcmp(cfg.layout, "us-intl") == 0);
	CHECK(cfg.size > 11.4f && cfg.size < 11.6f);
	CHECK(cfg.pad == 8);
	CHECK(cfg.pal.fg == 0xffffff);
	CHECK(cfg.pal.bg == 0x000000);
	CHECK(cfg.pal.cursor_text == 0x112233);
	CHECK(cfg.pal.cursor == 0xaabbcc);
	CHECK(cfg.pal.index[1] == 0xff0000);
	CHECK(cfg.pal.index[10] == 0x00ff00);
	CHECK(cfg.pal.index[4] == 0x0000ff);
	CHECK(cfg.pal.index[15] == 0xfedcba);

	/* foot.ini-style [colors-dark] and single-value cursor + cursor-text. */
	const char *dark =
		"[colors-dark]\n"
		"foreground = 123456\n"
		"cursor = 654321\n"
		"cursor-text = 010203\n";
	CHECK(tl_config_load_string(&cfg, dark) == 0);
	CHECK(cfg.pal.fg == 0x123456);
	CHECK(cfg.pal.cursor == 0x654321);
	CHECK(cfg.pal.cursor_text == 0x010203);

	/* Bad values leave previous values intact. */
	const char *bad =
		"[main]\n"
		"layout = bogus-layout\n"
		"size = 999\n"
		"pad = -5\n"
		"[colors]\n"
		"foreground = nothex\n"
		"background = 12345\n";
	CHECK(tl_config_load_string(&cfg, bad) == 0);
	CHECK(strcmp(cfg.layout, "us-intl") == 0);
	CHECK(cfg.size > 11.4f && cfg.size < 11.6f);
	CHECK(cfg.pad == 8);
	CHECK(cfg.pal.fg == 0x123456);
	CHECK(cfg.pal.bg == 0x000000);

	/* File loading and include support. */
	char inc_path[512], main_path[512];
	snprintf(inc_path, sizeof(inc_path), "%s/inc.ini", outdir);
	snprintf(main_path, sizeof(main_path), "%s/main.ini", outdir);
	FILE *f = fopen(inc_path, "w");
	CHECK(f != NULL);
	fputs("[colors-dark]\nbackground = 223344\n", f);
	fclose(f);
	f = fopen(main_path, "w");
	CHECK(f != NULL);
	fprintf(f, "[main]\ninclude = %s\npad = 6\n", inc_path);
	fclose(f);

	tl_config_default(&cfg);
	CHECK(tl_config_load_file(&cfg, main_path) == 0);
	CHECK(cfg.pad == 6);
	CHECK(cfg.pal.bg == 0x223344);
	CHECK(tl_config_load_file(&cfg, "/nonexistent/tam-last.ini") == -1);

	CHECK_DONE("test_config");
}
