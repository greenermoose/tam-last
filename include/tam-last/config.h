/* Configuration file (~/.config/tam-last/tam-last.ini) parsed with libtam's
 * tam_ini. Defaults match the Tamlinux dark theme. */
#ifndef TL_CONFIG_H
#define TL_CONFIG_H

#include <stddef.h>

#include "tam-last/render.h"

struct tl_config {
	char font[512];
	char font_bold[512];
	char font_italic[512];
	char font_bold_italic[512];
	char font_fallback[512];
	char layout[64];
	float size;
	int pad;
	struct tl_palette pal;
};

/* Fill cfg with compiled-in defaults (Tamlinux Tokyo Night theme, 9 pt, 2 px pad). */
void tl_config_default(struct tl_config *cfg);

/* Default path: $XDG_CONFIG_HOME/tam-last/tam-last.ini, else
 * $HOME/.config/tam-last/tam-last.ini. Empty string if neither is set. */
void tl_config_default_path(char *buf, size_t len);

/* Load configuration from an INI file or string. Returns 0 on success, -1 if
 * the file could not be opened. Invalid keys/values warn and keep defaults. */
int tl_config_load_file(struct tl_config *cfg, const char *path);
int tl_config_load_string(struct tl_config *cfg, const char *text);

#endif
