/* tam-last: start-up, options, server/client modes, and the one event loop.
 *
 * One process hosts 1 window (standalone) or 0..N windows (--server) over a
 * single Wayland display and shared font cache. The loop waits in poll() with
 * no timeout when idle, so idle terminals and an idle server use no CPU. */
#include <tam_sock.h>
#include <tam_uni.h>

#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "tam-last/action.h"
#include "tam-last/config.h"
#include "tam-last/font.h"
#include "tam-last/ipc.h"
#include "tam-last/keys.h"
#include "tam-last/osc.h"
#include "tam-last/proto.h"
#include "tam-last/pty.h"
#include "tam-last/render.h"
#include "tam-last/search.h"
#include "tam-last/select.h"
#include "tam-last/term.h"
#include "tam-last/url.h"
#include "tam-last/window.h"

#ifndef TL_VERSION
#define TL_VERSION "unknown"
#endif

#define DEFAULT_COLS 80
#define DEFAULT_ROWS 24
#define READ_CHUNK 65536
#define TL_BTN_LEFT 0x110u
#define TL_BTN_RIGHT 0x111u
#define TL_BTN_MIDDLE 0x112u
#define TL_KEYS_BAR_ROWS 4
#define TL_OSC52_READ_MAX 65536u
#define TL_SYNC_TIMEOUT_MS 1000
#define TL_SERVER_MAX_WINDOWS 64
#define TL_SERVER_MAX_PENDING 16

struct tl_server;

struct tl_session {
	struct tl_server *srv;
	struct tl_window win;
	struct tl_term term;
	struct tl_font *font; /* borrowed from srv->fonts */
	struct tl_pty pty;
	struct tl_kbd kbd;
	const struct tl_layout *key_layout;
	struct tl_compose compose;
	struct tl_config cfg;
	char font_path[512];
	float pt;
	int pad_logical;
	/* The scale in 120ths (fractional-scale-v1). The window is lw x lh
	 * logical pixels; the buffer and everything drawn in it are
	 * width x height device pixels, the logical size times the scale. */
	uint32_t scale120;
	int lw, lh;
	int width, height, pad;
	bool focused;
	bool prev_focused;
	bool need_draw;
	bool closing;
	bool pty_open;
	bool child_done;
	int exit_status;
	int client_fd;
	int prev_cx, prev_cy;
	bool *buf_dirty[2];
	int buf_dirty_rows;
	bool buf_full[2];
	int32_t repeat_rate, repeat_delay;
	uint32_t repeat_code;
	struct timespec repeat_at;
	struct tl_sel sel;
	int ptr_cx, ptr_cy;
	bool ptr_inside;
	uint32_t ptr_buttons;
	uint32_t last_click_time;
	int click_count;
	double axis_acc;
	int paste_fd;
	bool paste_bracketed;
	/* Async OSC 52 clipboard read state. */
	int osc52_fd;
	char *osc52_buf;
	size_t osc52_len;
	size_t osc52_cap;
	bool osc52_st;
	char osc52_target[16];
	bool keys_mode;
	int keys_bar_rows;
	char keys_lines[TL_KEYS_BAR_ROWS][512];
	int keys_count;
	bool buf_bar_dirty[2];
	uint32_t shortcut_code;
	/* Active IME (text-input-v3) preedit state. */
	char *preedit_text;
	int32_t preedit_cbeg, preedit_cend;
	int preedit_y0, preedit_y1;
	/* Synchronized output (DEC 2026) safety timeout. */
	bool sync_armed;
	struct timespec sync_deadline;
	/* Interactive modes (0.2.2): scrollback search, URL hints, notifications. */
	struct tl_search search;
	struct tl_url_mode urls;
	enum tl_notify_policy notify_policy;
};

struct tl_pending_client {
	int fd;
	uint8_t *buf;
	size_t len;
	size_t want;
	struct timespec deadline;
};

struct tl_server {
	struct tl_display dpy;
	struct tl_font_cache fonts;
	struct tl_actions actions;
	int listen_fd;
	char socket_path[256];
	struct tl_session *sessions[TL_SERVER_MAX_WINDOWS];
	size_t nsessions;
	struct tl_pending_client pending[TL_SERVER_MAX_PENDING];
	bool stop;
	int last_exit_status;
};

static int sig_pipe[2] = { -1, -1 };

static void on_sigchld(int sig)
{
	(void)sig;
	int saved = errno;
	(void)!write(sig_pipe[1], "c", 1);
	errno = saved;
}

static void on_sigterm(int sig)
{
	(void)sig;
	int saved = errno;
	(void)!write(sig_pipe[1], "t", 1);
	errno = saved;
}

static void usage(FILE *out)
{
	fputs("usage: tam-last [--server | --client] [-S SOCKET] [-N] [-c CONFIG] [-f FONT]\n"
	      "                [-s POINTS] [-l LAYOUT] [--term NAME] [--osc52 POLICY]\n"
	      "                [--notify POLICY] [--keys] [-e COMMAND [ARG...]]\n"
	      "\n"
	      "      --server       run as a multi-window server with shared font cache\n"
	      "      --client       open a new window on a running server (also tam-last-client)\n"
	      "  -S, --socket PATH  Unix socket path for --server / --client\n"
	      "  -N, --no-wait      in --client mode, exit once the window is created\n"
	      "  -c, --config FILE  configuration file (default: ~/.config/tam-last/tam-last.ini)\n"
	      "  -f, --font FILE    a TrueType font file (default: $TAM_LAST_FONT,\n"
	      "                     then JetBrains Mono, DejaVu Sans Mono, ...)\n"
	      "  -s, --size POINTS  font size (default 9)\n"
	      "  -l, --layout NAME  keyboard layout (default: us; 'list' prints available)\n"
	      "      --term NAME    override $TERM (default: tam-last if installed, else xterm-256color)\n"
	      "      --osc52 POLICY clipboard policy: write/copy (default), disabled/none,\n"
	      "                     read/paste, both/copy-paste\n"
	      "      --notify POL   desktop notifications: unfocused (default), yes/always, no/disabled\n"
	      "      --keys         trace every key event to stderr and a bottom bar\n"
	      "  -e COMMAND ...     run COMMAND instead of $SHELL\n"
	      "  -v, --version      print the version\n"
	      "  -h, --help         this help\n", out);
}

static void list_layouts(FILE *out)
{
	for (size_t i = 0; i < tl_layout_count(); i++) {
		const struct tl_layout *l = tl_layout_at(i);
		if (l != NULL)
			fprintf(out, "%s\n", l->name);
	}
}

static void ts_add_ms(struct timespec *t, long ms)
{
	t->tv_sec += ms / 1000;
	t->tv_nsec += (ms % 1000) * 1000000L;
	if (t->tv_nsec >= 1000000000L) {
		t->tv_sec++;
		t->tv_nsec -= 1000000000L;
	}
}

static long ms_until(const struct timespec *t)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	long ms = (long)(t->tv_sec - now.tv_sec) * 1000 +
		(t->tv_nsec - now.tv_nsec) / 1000000L;
	return ms < 0 ? 0 : ms;
}

static void sync_dirty(struct tl_session *a)
{
	int rows = a->term.rows;
	if (a->buf_dirty_rows != rows) {
		for (int i = 0; i < 2; i++) {
			free(a->buf_dirty[i]);
			a->buf_dirty[i] = calloc((size_t)rows, sizeof(bool));
			a->buf_full[i] = true;
		}
		a->buf_dirty_rows = rows;
	}
	if (a->term.full_dirty) {
		a->buf_full[0] = a->buf_full[1] = true;
		a->term.full_dirty = false;
	}
	for (int y = 0; y < rows; y++) {
		if (a->term.dirty_rows[y]) {
			for (int i = 0; i < 2; i++)
				if (a->buf_dirty[i] != NULL)
					a->buf_dirty[i][y] = true;
			a->term.dirty_rows[y] = false;
		}
	}
	if (a->prev_cy != a->term.cy || a->prev_cx != a->term.cx ||
	    a->prev_focused != a->focused) {
		for (int i = 0; i < 2; i++) {
			if (a->buf_dirty[i] == NULL)
				continue;
			if (a->prev_cy >= 0 && a->prev_cy < rows)
				a->buf_dirty[i][a->prev_cy] = true;
			if (a->term.cy >= 0 && a->term.cy < rows)
				a->buf_dirty[i][a->term.cy] = true;
		}
		a->prev_cx = a->term.cx;
		a->prev_cy = a->term.cy;
		a->prev_focused = a->focused;
	}
}

static void mark_rows_dirty(struct tl_session *a, int y0, int y1)
{
	int rows = a->buf_dirty_rows;
	if (y0 < 0)
		y0 = 0;
	if (y1 >= rows)
		y1 = rows - 1;
	for (int i = 0; i < 2; i++) {
		if (a->buf_dirty[i] == NULL)
			continue;
		for (int y = y0; y <= y1; y++)
			a->buf_dirty[i][y] = true;
	}
}

static void update_ime_cursor_rect(struct tl_session *a)
{
	if (!tl_window_has_text_input(&a->win))
		return;
	int dx = a->pad + a->term.cx * a->font->cell_w;
	int dy = a->pad + a->term.cy * a->font->cell_h;
	int dw = a->font->cell_w;
	int dh = a->font->cell_h;
	int32_t lx = (int32_t)(((long)dx * 120) / a->scale120);
	int32_t ly = (int32_t)(((long)dy * 120) / a->scale120);
	int32_t lw = (int32_t)(((long)dw * 120 + a->scale120 - 1) / a->scale120);
	int32_t lh = (int32_t)(((long)dh * 120 + a->scale120 - 1) / a->scale120);
	tl_window_set_cursor_rect(&a->win, lx, ly, lw, lh);
}

static void draw(struct tl_session *a, bool force)
{
	if (!a->win.configured)
		return;
	if (!force && a->term.sync_output) {
		a->need_draw = true;
		return;
	}
	if (tl_window_frame_pending(&a->win) && !force) {
		a->need_draw = true;
		return;
	}
	sync_dirty(a);
	struct tl_buffer *b = tl_window_buffer(&a->win, a->width, a->height);
	if (b == NULL) {
		a->need_draw = true; /* both busy; try again on release */
		return;
	}
	int idx = (b == &a->win.bufs[1]) ? 1 : 0;
	bool full = a->buf_full[idx] || a->buf_dirty[idx] == NULL;
	bool show_preedit = a->preedit_text != NULL &&
		a->preedit_text[0] != '\0' &&
		(a->term.alt_screen || a->term.view_offset == 0);
	if (show_preedit && !full && a->buf_dirty[idx] != NULL) {
		int y0 = a->preedit_y0 >= 0 ? a->preedit_y0 : a->term.cy;
		int y1 = a->preedit_y1 >= 0 ? a->preedit_y1 : a->term.cy;
		if (a->term.cy < y0)
			y0 = a->term.cy;
		if (a->term.cy > y1)
			y1 = a->term.cy;
		for (int y = y0; y <= y1 && y < a->buf_dirty_rows; y++)
			if (y >= 0)
				a->buf_dirty[idx][y] = true;
	}
	tl_render_rows(&a->term, a->font, &a->term.pal, b->pixels, b->width,
		b->height, a->pad, a->focused, &a->sel,
		a->search.active ? &a->search : NULL, a->buf_dirty[idx], full);
	if (a->urls.active)
		tl_render_urls(&a->term, a->font, &a->term.pal, b->pixels,
			b->width, b->height, a->pad, &a->urls);
	if (a->search.active || a->urls.active) {
		char bar[512];
		if (a->search.active)
			tl_search_format_bar(&a->search, bar, sizeof(bar));
		else
			tl_url_format_bar(&a->urls, bar, sizeof(bar));
		tl_render_bar(a->font, &a->term.pal, b->pixels, b->width,
			b->height, a->pad, a->term.rows - 1, a->term.cols, bar);
		if (!full && a->buf_dirty[idx] != NULL && a->term.rows > 0)
			a->buf_dirty[idx][a->term.rows - 1] = true;
	}
	if (show_preedit) {
		struct tl_preedit pe = {
			a->preedit_text, a->preedit_cbeg, a->preedit_cend
		};
		int last_y = tl_render_preedit(&a->term, a->font, &a->term.pal,
			b->pixels, b->width, b->height, a->pad, &pe);
		if (last_y >= 0) {
			a->preedit_y0 = a->term.cy;
			a->preedit_y1 = last_y;
			if (!full && a->buf_dirty[idx] != NULL) {
				for (int y = a->preedit_y0;
				     y <= last_y && y < a->buf_dirty_rows; y++)
					if (y >= 0)
						a->buf_dirty[idx][y] = true;
			}
		}
	} else {
		a->preedit_y0 = a->preedit_y1 = -1;
	}
	update_ime_cursor_rect(a);
	bool bar_drawn = false;
	if (a->keys_mode && a->keys_bar_rows > 0 &&
	    (full || a->buf_bar_dirty[idx])) {
		for (int r = 0; r < a->keys_bar_rows; r++) {
			const char *text = r < a->keys_count ?
				a->keys_lines[r] : "";
			tl_render_bar(a->font, &a->term.pal, b->pixels, b->width,
				b->height, a->pad, a->term.rows + r,
				a->term.cols, text);
		}
		a->buf_bar_dirty[idx] = false;
		bar_drawn = true;
	}

	struct tl_rect rects[64];
	size_t nrects = 0;
	if (!full) {
		int cw = a->font->cell_w, ch = a->font->cell_h;
		int y = 0;
		bool overflow = false;
		while (y < a->term.rows) {
			if (!a->buf_dirty[idx][y]) {
				y++;
				continue;
			}
			int y0 = y;
			while (y < a->term.rows && a->buf_dirty[idx][y])
				y++;
			if (nrects == sizeof(rects) / sizeof(rects[0])) {
				nrects = 0;
				overflow = true;
				break;
			}
			rects[nrects++] = (struct tl_rect){
				a->pad, a->pad + y0 * ch,
				a->term.cols * cw, (y - y0) * ch
			};
		}
		if (bar_drawn && !overflow) {
			if (nrects == sizeof(rects) / sizeof(rects[0])) {
				nrects = 0;
			} else {
				rects[nrects++] = (struct tl_rect){
					0, a->pad + a->term.rows * ch,
					a->width, a->keys_bar_rows * ch
				};
			}
		}
	}
	tl_window_present_damage(&a->win, b, a->lw, a->lh, rects, nrects);
	a->buf_full[idx] = false;
	if (a->buf_dirty[idx] != NULL)
		memset(a->buf_dirty[idx], 0,
			(size_t)a->buf_dirty_rows * sizeof(bool));
	a->need_draw = false;
	a->term.dirty = false;
}

static void trace_append_line(struct tl_session *a, const char *line)
{
	fputs(line, stderr);
	fputc('\n', stderr);
	fflush(stderr);
	int cap = a->keys_bar_rows > 0 ? a->keys_bar_rows : TL_KEYS_BAR_ROWS;
	if (cap > TL_KEYS_BAR_ROWS)
		cap = TL_KEYS_BAR_ROWS;
	if (a->keys_count >= cap) {
		for (int i = 1; i < cap; i++)
			memcpy(a->keys_lines[i - 1], a->keys_lines[i],
				sizeof(a->keys_lines[0]));
		a->keys_count = cap - 1;
	}
	snprintf(a->keys_lines[a->keys_count++], sizeof(a->keys_lines[0]),
		"%s", line);
	a->buf_bar_dirty[0] = a->buf_bar_dirty[1] = true;
	a->need_draw = true;
}

static void trace_key(struct tl_session *a, uint32_t code, enum tl_key_event ev,
	const struct tl_key_modes *m, const char *seq, size_t n,
	const char *note)
{
	char line[512];
	tl_key_trace_format(line, sizeof(line), code, tl_kbd_mods(&a->kbd),
		ev, m, seq, n, note);
	trace_append_line(a, line);
}

static void trace_ime(struct tl_session *a, const char *kind, const char *text,
	size_t len, int32_t cbeg, int32_t cend)
{
	char line[512];
	tl_key_trace_ime(line, sizeof(line), kind, text, len, cbeg, cend);
	trace_append_line(a, line);
}

static struct tl_key_modes key_modes(const struct tl_session *a)
{
	struct tl_key_modes m = {
		a->term.app_cursor, a->term.app_keypad, a->kbd.num, a->kbd.caps,
		tl_term_kitty_kbd_flags(&a->term), a->key_layout,
	};
	return m;
}

static void trace_shortcut(struct tl_session *a, uint32_t code,
	const char *note)
{
	a->shortcut_code = code;
	if (!a->keys_mode)
		return;
	struct tl_key_modes m = key_modes(a);
	trace_key(a, code, TL_KEY_PRESS, &m, NULL, 0, note);
}

static void send_key(struct tl_session *a, uint32_t code, enum tl_key_event ev)
{
	struct tl_key_modes m = key_modes(a);
	char seq[TL_KEY_SEQ_MAX];
	size_t n = tl_key_encode_event(code, tl_kbd_mods(&a->kbd), ev, &m, seq);
	if (n > 0)
		(void)tl_pty_queue(&a->pty, seq, n);
	if (a->keys_mode)
		trace_key(a, code, ev, &m, seq, n, NULL);
}

/* Logical to device pixels, rounded as fractional-scale-v1 asks. */
static int to_device(const struct tl_session *a, int v)
{
	return (int)(((long)v * a->scale120 + 60) / 120);
}

static int to_logical(const struct tl_session *a, int v)
{
	return (int)(((long)v * 120 + a->scale120 - 1) / a->scale120);
}

/* Fit the grid to the window's logical size at the current scale. */
static void layout(struct tl_session *a)
{
	a->pad = to_device(a, a->pad_logical);
	a->width = to_device(a, a->lw);
	a->height = to_device(a, a->lh);
	int cols = (a->width - 2 * a->pad) / a->font->cell_w;
	int total_rows = (a->height - 2 * a->pad) / a->font->cell_h;
	int bar_rows = 0;
	if (a->keys_mode && total_rows > 1) {
		bar_rows = total_rows - 1;
		if (bar_rows > TL_KEYS_BAR_ROWS)
			bar_rows = TL_KEYS_BAR_ROWS;
	}
	a->keys_bar_rows = bar_rows;
	int rows = total_rows - bar_rows;
	if (cols < 1)
		cols = 1;
	if (rows < 1)
		rows = 1;
	if (cols != a->term.cols || rows != a->term.rows) {
		if (tl_term_resize(&a->term, cols, rows) == 0)
			tl_pty_resize(&a->pty, cols, rows, a->width, a->height);
	}
}

static void on_configure(void *ud, int width, int height)
{
	struct tl_session *a = ud;
	if (width <= 0 || height <= 0) {
		int want_rows = a->term.rows +
			(a->keys_mode ? TL_KEYS_BAR_ROWS : 0);
		width = to_logical(a, a->term.cols * a->font->cell_w +
			2 * to_device(a, a->pad_logical));
		height = to_logical(a, want_rows * a->font->cell_h +
			2 * to_device(a, a->pad_logical));
	}
	a->lw = width;
	a->lh = height;
	layout(a);
	draw(a, true);
}

static void build_font_key(const struct tl_session *a, struct tl_font_key *key)
{
	memset(key, 0, sizeof(*key));
	snprintf(key->path, sizeof(key->path), "%s", a->font_path);
	snprintf(key->bold, sizeof(key->bold), "%s", a->cfg.font_bold);
	snprintf(key->italic, sizeof(key->italic), "%s", a->cfg.font_italic);
	snprintf(key->bold_italic, sizeof(key->bold_italic), "%s",
		a->cfg.font_bold_italic);
	snprintf(key->fallback, sizeof(key->fallback), "%s",
		a->cfg.font_fallback);
	key->pt_tenths = (int)(a->pt * 10.0f + 0.5f);
	key->dpi = (int)((96L * a->scale120 + 60) / 120);
}

static void on_scale(void *ud, uint32_t scale120)
{
	struct tl_session *a = ud;
	if (scale120 == a->scale120)
		return;
	uint32_t old_scale = a->scale120;
	a->scale120 = scale120;
	struct tl_font_key key;
	build_font_key(a, &key);
	char err[300];
	struct tl_font *nf = tl_font_cache_acquire(&a->srv->fonts, &key, err,
		sizeof(err));
	if (nf == NULL) {
		fprintf(stderr, "tam-last: %s\n", err);
		a->scale120 = old_scale;
		return;
	}
	struct tl_font *old_font = a->font;
	a->font = nf;
	tl_font_cache_release(&a->srv->fonts, old_font);
	layout(a);
	a->term.full_dirty = true;
	a->term.dirty = true;
	draw(a, true);
}

static void on_close(void *ud)
{
	((struct tl_session *)ud)->closing = true;
}

static void on_frame(void *ud)
{
	struct tl_session *a = ud;
	if (a->need_draw || a->term.dirty)
		draw(a, false);
}

static void update_cursor_shape(struct tl_session *a)
{
	bool mouse_app = a->term.mouse_mode != TL_MOUSE_NONE &&
		!(tl_kbd_mods(&a->kbd) & TL_MOD_SHIFT);
	uint32_t shape = mouse_app ? TL_CURSOR_SHAPE_DEFAULT :
		a->term.hover_link_id != 0 ? TL_CURSOR_SHAPE_POINTER :
		TL_CURSOR_SHAPE_TEXT;
	tl_window_set_cursor(&a->win, shape);
}

static void send_osc52_reply(struct tl_session *a, const char *target, bool st,
	const void *data, size_t len)
{
	char *b64 = NULL;
	size_t b64_len = 0;
	if (tl_base64_encode(data, len, &b64, &b64_len) != 0)
		return;
	char hdr[32];
	int hlen = snprintf(hdr, sizeof(hdr), "\033]52;%s;", target);
	if (hlen > 0 && (size_t)hlen < sizeof(hdr)) {
		(void)tl_pty_queue(&a->pty, hdr, (size_t)hlen);
		if (b64_len > 0)
			(void)tl_pty_queue(&a->pty, b64, b64_len);
		if (st)
			(void)tl_pty_queue(&a->pty, "\033\\", 2);
		else
			(void)tl_pty_queue(&a->pty, "\007", 1);
	}
	free(b64);
}

static void finish_osc52_read(struct tl_session *a)
{
	send_osc52_reply(a, a->osc52_target, a->osc52_st,
		a->osc52_buf != NULL ? a->osc52_buf : "", a->osc52_len);
	if (a->osc52_fd >= 0) {
		close(a->osc52_fd);
		a->osc52_fd = -1;
	}
	a->osc52_len = 0;
}

static void drain_osc52_read(struct tl_session *a)
{
	char buf[4096];
	for (;;) {
		ssize_t n = read(a->osc52_fd, buf, sizeof(buf));
		if (n > 0) {
			if (a->osc52_len < TL_OSC52_READ_MAX) {
				size_t take = (size_t)n;
				if (a->osc52_len + take > TL_OSC52_READ_MAX)
					take = TL_OSC52_READ_MAX - a->osc52_len;
				if (a->osc52_len + take > a->osc52_cap) {
					size_t ncap = a->osc52_cap == 0 ? 4096 :
						a->osc52_cap * 2;
					while (ncap < a->osc52_len + take)
						ncap *= 2;
					if (ncap > TL_OSC52_READ_MAX)
						ncap = TL_OSC52_READ_MAX;
					char *nb = realloc(a->osc52_buf, ncap);
					if (nb != NULL) {
						a->osc52_buf = nb;
						a->osc52_cap = ncap;
					} else {
						take = 0;
					}
				}
				if (take > 0) {
					memcpy(a->osc52_buf + a->osc52_len, buf,
						take);
					a->osc52_len += take;
				}
			}
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		finish_osc52_read(a);
		return;
	}
}

static void handle_term_osc(struct tl_session *a)
{
	if (a->term.title_dirty) {
		tl_window_set_title(&a->win,
			a->term.title[0] != '\0' ? a->term.title : "tam-last");
		a->term.title_dirty = false;
	}
	if (a->term.osc52_write_pending) {
		const char *data = a->term.osc52_write_data != NULL ?
			a->term.osc52_write_data : "";
		if (a->term.osc52_write_clip)
			tl_window_set_clipboard(&a->win, data,
				a->term.osc52_write_len);
		if (a->term.osc52_write_prim)
			tl_window_set_primary(&a->win, data,
				a->term.osc52_write_len);
		tl_term_osc52_clear_write(&a->term);
	}
	if (a->term.osc52_read_pending) {
		a->term.osc52_read_pending = false;
		if (a->osc52_fd >= 0) {
			close(a->osc52_fd);
			a->osc52_fd = -1;
		}
		int fd = tl_window_paste(&a->win, a->term.osc52_read_prim);
		if (fd >= 0) {
			a->osc52_fd = fd;
			a->osc52_len = 0;
			a->osc52_st = a->term.osc52_read_st;
			snprintf(a->osc52_target, sizeof(a->osc52_target),
				"%s", a->term.osc52_read_target);
		} else {
			const struct tl_display *dpy = &a->srv->dpy;
			const char *local = a->term.osc52_read_prim ?
				dpy->prim_text : dpy->clip_text;
			size_t local_len = a->term.osc52_read_prim ?
				dpy->prim_len : dpy->clip_len;
			send_osc52_reply(a, a->term.osc52_read_target,
				a->term.osc52_read_st,
				local != NULL ? local : "",
				local != NULL ? local_len : 0);
		}
	}
	if (a->term.notify.pending) {
		bool allow = false;
		if (a->notify_policy == TL_NOTIFY_ALWAYS)
			allow = !a->term.notify.only_unfocused || !a->focused;
		else if (a->notify_policy == TL_NOTIFY_UNFOCUSED)
			allow = !a->focused;
		if (allow)
			(void)tl_action_notify(&a->srv->actions,
				&a->term.notify, NULL, 0);
		tl_term_notify_clear(&a->term);
	}
}

static void start_paste(struct tl_session *a, bool primary)
{
	if (a->paste_fd >= 0) {
		close(a->paste_fd);
		a->paste_fd = -1;
	}
	int fd = tl_window_paste(&a->win, primary);
	if (fd < 0 && primary)
		fd = tl_window_paste(&a->win, false);
	if (fd < 0)
		return;
	a->paste_fd = fd;
	a->paste_bracketed = a->term.bracketed_paste;
	if (a->paste_bracketed)
		(void)tl_pty_queue(&a->pty, "\033[200~", 6);
}

static void drain_paste(struct tl_session *a)
{
	char buf[4096];
	for (;;) {
		ssize_t n = read(a->paste_fd, buf, sizeof(buf));
		if (n > 0) {
			if (a->paste_bracketed) {
				for (ssize_t i = 0; i < n; i++)
					if (buf[i] == '\033')
						buf[i] = ' ';
			}
			(void)tl_pty_queue(&a->pty, buf, (size_t)n);
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		if (a->paste_bracketed)
			(void)tl_pty_queue(&a->pty, "\033[201~", 6);
		a->paste_bracketed = false;
		close(a->paste_fd);
		a->paste_fd = -1;
		return;
	}
}

static bool mouse_reporting(const struct tl_session *a)
{
	return a->term.mouse_mode != TL_MOUSE_NONE &&
		!(tl_kbd_mods(&a->kbd) & TL_MOD_SHIFT);
}

static void ptr_to_cell(const struct tl_session *a, double lx, double ly,
	int *cx, int *cy)
{
	double dx = lx * (double)a->scale120 / 120.0;
	double dy = ly * (double)a->scale120 / 120.0;
	int x = (int)((dx - a->pad) / a->font->cell_w);
	int y = (int)((dy - a->pad) / a->font->cell_h);
	if (x < 0)
		x = 0;
	if (x >= a->term.cols)
		x = a->term.cols - 1;
	if (y < 0)
		y = 0;
	if (y >= a->term.rows)
		y = a->term.rows - 1;
	*cx = x;
	*cy = y;
}

static int mouse_mod_bits(const struct tl_session *a)
{
	unsigned m = tl_kbd_mods(&a->kbd);
	int bits = 0;
	if (m & TL_MOD_SHIFT)
		bits |= 4;
	if (m & TL_MOD_ALT)
		bits |= 8;
	if (m & TL_MOD_CTRL)
		bits |= 16;
	return bits;
}

static void send_mouse(struct tl_session *a, int cb, int cx, int cy,
	bool release)
{
	cb |= mouse_mod_bits(a);
	if (a->term.mouse_sgr) {
		char buf[48];
		int n = snprintf(buf, sizeof(buf), "\033[<%d;%d;%d%c",
			cb, cx + 1, cy + 1, release ? 'm' : 'M');
		if (n > 0)
			(void)tl_pty_queue(&a->pty, buf, (size_t)n);
		return;
	}
	if (release)
		cb = (cb & ~3) | 3;
	int x = cx + 1, y = cy + 1;
	if (x > 223)
		x = 223;
	if (y > 223)
		y = 223;
	unsigned char pkt[6] = {
		0x1b, '[', 'M',
		(unsigned char)(32 + cb),
		(unsigned char)(32 + x),
		(unsigned char)(32 + y),
	};
	(void)tl_pty_queue(&a->pty, pkt, sizeof(pkt));
}

static int view_abs_y(const struct tl_session *a, int cy)
{
	int sb = a->term.alt_screen ? 0 : a->term.sb_len;
	int vo = a->term.alt_screen ? 0 : a->term.view_offset;
	return sb - vo + cy;
}

static void on_key(void *ud, uint32_t code, bool pressed)
{
	struct tl_session *a = ud;
	tl_kbd_key(&a->kbd, code, pressed);
	update_cursor_shape(a);
	if (!pressed) {
		if (code == a->repeat_code)
			a->repeat_code = 0;
		if (code == a->shortcut_code && a->shortcut_code != 0) {
			a->shortcut_code = 0;
			if (a->keys_mode) {
				struct tl_key_modes m = key_modes(a);
				trace_key(a, code, TL_KEY_RELEASE, &m,
					NULL, 0, NULL);
			}
			return;
		}
		if ((tl_term_kitty_kbd_flags(&a->term) & TL_KITTY_EVENTS) ||
		    a->keys_mode)
			send_key(a, code, TL_KEY_RELEASE);
		return;
	}
	unsigned mods = tl_kbd_mods(&a->kbd);
	if (a->urls.active) {
		if (code == TL_KEY_ESC ||
		    (mods == TL_MOD_CTRL &&
		     (code == TL_KEY_C || code == TL_KEY_G))) {
			tl_url_end(&a->urls);
			tl_term_mark_all(&a->term);
			draw(a, false);
			return;
		}
		if (code == TL_KEY_BACKSPACE) {
			tl_url_pop(&a->urls);
			tl_term_mark_all(&a->term);
			draw(a, false);
			return;
		}
		if (!tl_key_is_modifier(code) &&
		    (mods & (TL_MOD_CTRL | TL_MOD_ALT | TL_MOD_META)) == 0) {
			struct tl_key_modes m = key_modes(a);
			uint32_t sym = tl_key_char(code, mods & ~TL_MOD_SHIFT,
				&m);
			if (sym >= 0x20u && sym < 0x7fu) {
				const char *uri = NULL;
				enum tl_url_step st = tl_url_feed(&a->urls,
					(char)sym, &uri);
				if (st == TL_URL_MATCHED && uri != NULL) {
					char copy[TL_LINK_URI_MAX + 1];
					snprintf(copy, sizeof(copy), "%s", uri);
					tl_url_end(&a->urls);
					if (mods & TL_MOD_SHIFT) {
						size_t n = strlen(copy);
						tl_window_set_clipboard(&a->win,
							copy, n);
						tl_window_set_primary(&a->win,
							copy, n);
					} else {
						(void)tl_action_open_url(copy);
					}
					tl_term_mark_all(&a->term);
					draw(a, false);
				} else if (st == TL_URL_PENDING) {
					tl_term_mark_all(&a->term);
					draw(a, false);
				}
			}
		}
		return;
	}
	if (a->search.active) {
		if (code == TL_KEY_ESC ||
		    (mods == TL_MOD_CTRL &&
		     (code == TL_KEY_C || code == TL_KEY_G))) {
			int saved = a->search.saved_view_offset;
			tl_search_end(&a->search);
			if (!a->term.alt_screen) {
				if (saved > a->term.sb_len)
					saved = a->term.sb_len;
				if (saved < 0)
					saved = 0;
				a->term.view_offset = saved;
			}
			tl_term_mark_all(&a->term);
			draw(a, false);
			return;
		}
		if ((code == TL_KEY_ENTER || code == TL_KEY_KPENTER) &&
		    (mods & TL_MOD_SHIFT) == 0) {
			const struct tl_search_match *m =
				tl_search_current(&a->search);
			if (m != NULL) {
				tl_sel_start(&a->sel, &a->term, m->x0, m->abs_y,
					TL_SEL_CHAR);
				tl_sel_update(&a->sel, &a->term, m->x1,
					m->abs_y);
				tl_sel_end(&a->sel);
				size_t slen = 0;
				char *text = tl_sel_extract(&a->term, &a->sel,
					&slen);
				if (text != NULL && slen > 0)
					tl_window_set_primary(&a->win, text,
						slen);
				free(text);
			}
			tl_search_end(&a->search);
			tl_term_mark_all(&a->term);
			draw(a, false);
			return;
		}
		if (code == TL_KEY_UP ||
		    ((code == TL_KEY_ENTER || code == TL_KEY_KPENTER) &&
		     (mods & TL_MOD_SHIFT) != 0) ||
		    (code == TL_KEY_R && (mods & TL_MOD_CTRL) != 0)) {
			tl_search_next(&a->search, -1);
			tl_search_sync_view(&a->search, &a->term);
			tl_term_mark_all(&a->term);
			draw(a, false);
			return;
		}
		if (code == TL_KEY_DOWN ||
		    (mods == TL_MOD_CTRL &&
		     (code == TL_KEY_S || code == TL_KEY_N))) {
			tl_search_next(&a->search, 1);
			tl_search_sync_view(&a->search, &a->term);
			tl_term_mark_all(&a->term);
			draw(a, false);
			return;
		}
		if (code == TL_KEY_BACKSPACE) {
			tl_search_pop_cp(&a->search, &a->term);
			tl_search_sync_view(&a->search, &a->term);
			tl_term_mark_all(&a->term);
			draw(a, false);
			return;
		}
		if (mods == TL_MOD_CTRL && code == TL_KEY_W) {
			tl_search_pop_word(&a->search, &a->term);
			tl_search_sync_view(&a->search, &a->term);
			tl_term_mark_all(&a->term);
			draw(a, false);
			return;
		}
		if (mods == TL_MOD_CTRL && code == TL_KEY_U) {
			tl_search_clear_query(&a->search, &a->term);
			tl_term_mark_all(&a->term);
			draw(a, false);
			return;
		}
		if (!tl_key_is_modifier(code) &&
		    (mods & (TL_MOD_CTRL | TL_MOD_ALT | TL_MOD_META)) == 0) {
			struct tl_key_modes m = key_modes(a);
			uint32_t sym = tl_key_char(code, mods, &m);
			if (sym != 0 && !(sym & TL_KEY_DEAD_BIT)) {
				tl_search_push_cp(&a->search, &a->term, sym);
				tl_search_sync_view(&a->search, &a->term);
				tl_term_mark_all(&a->term);
				draw(a, false);
			}
		}
		return;
	}
	int page = a->term.rows / 2 > 0 ? a->term.rows / 2 : 1;
	if (code == TL_KEY_R && mods == (TL_MOD_CTRL | TL_MOD_SHIFT)) {
		trace_shortcut(a, code, "search");
		tl_url_end(&a->urls);
		tl_search_start(&a->search, &a->term);
		tl_term_mark_all(&a->term);
		draw(a, false);
		return;
	}
	if (code == TL_KEY_O && mods == (TL_MOD_CTRL | TL_MOD_SHIFT)) {
		trace_shortcut(a, code, "url-mode");
		tl_search_end(&a->search);
		if (tl_url_start(&a->urls, &a->term) > 0) {
			tl_term_mark_all(&a->term);
			draw(a, false);
		}
		return;
	}
	if (code == TL_KEY_PAGEUP && mods == TL_MOD_SHIFT) {
		trace_shortcut(a, code, "scroll-up");
		tl_term_scroll_view(&a->term, page);
		draw(a, false);
		return;
	}
	if (code == TL_KEY_PAGEDOWN && mods == TL_MOD_SHIFT) {
		trace_shortcut(a, code, "scroll-down");
		tl_term_scroll_view(&a->term, -page);
		draw(a, false);
		return;
	}
	if ((code == TL_KEY_C && mods == (TL_MOD_CTRL | TL_MOD_SHIFT)) ||
	    (code == TL_KEY_INSERT && mods == TL_MOD_CTRL)) {
		trace_shortcut(a, code, "copy");
		size_t len = 0;
		char *text = tl_sel_extract(&a->term, &a->sel, &len);
		if (text != NULL && len > 0)
			tl_window_set_clipboard(&a->win, text, len);
		free(text);
		return;
	}
	if (code == TL_KEY_V && mods == (TL_MOD_CTRL | TL_MOD_SHIFT)) {
		trace_shortcut(a, code, "paste");
		tl_term_reset_view(&a->term);
		start_paste(a, false);
		return;
	}
	if (code == TL_KEY_INSERT && mods == TL_MOD_SHIFT) {
		trace_shortcut(a, code, "paste");
		tl_term_reset_view(&a->term);
		start_paste(a, false);
		return;
	}
	if (!tl_key_is_modifier(code))
		tl_term_reset_view(&a->term);

	bool ctrl_alt_meta = (mods & (TL_MOD_CTRL | TL_MOD_ALT | TL_MOD_META)) != 0;
	if (!ctrl_alt_meta && code == TL_KEY_COMPOSE) {
		uint32_t dummy = 0;
		(void)tl_compose_feed(&a->compose, TL_COMPOSE_START, &dummy);
		if (a->keys_mode) {
			struct tl_key_modes m = key_modes(a);
			trace_key(a, code, TL_KEY_PRESS, &m, NULL, 0,
				"compose");
		}
		return;
	}
	if (tl_compose_active(&a->compose) && code == TL_KEY_ESC) {
		tl_compose_reset(&a->compose);
		if (a->keys_mode) {
			struct tl_key_modes m = key_modes(a);
			trace_key(a, code, TL_KEY_PRESS, &m, NULL, 0,
				"compose-cancel");
		}
		return;
	}
	if (!ctrl_alt_meta && !tl_key_is_modifier(code)) {
		struct tl_key_modes m = key_modes(a);
		uint32_t sym = tl_key_char(code, mods, &m);
		if (sym != 0 &&
		    ((sym & TL_KEY_DEAD_BIT) != 0 ||
		     tl_compose_active(&a->compose))) {
			uint32_t cp = 0;
			enum tl_compose_status st =
				tl_compose_feed(&a->compose, sym, &cp);
			if (st == TL_COMPOSE_PENDING) {
				if (a->keys_mode) {
					const char *dn = tl_dead_key_name(sym);
					trace_key(a, code, TL_KEY_PRESS, &m,
						NULL, 0,
						dn != NULL ? dn : "compose");
				}
				return;
			}
			if (st == TL_COMPOSE_DONE) {
				char seq[TL_KEY_SEQ_MAX];
				size_t n = tl_key_encode_text(cp, seq);
				if (n > 0)
					(void)tl_pty_queue(&a->pty, seq, n);
				if (a->keys_mode) {
					char note[32];
					snprintf(note, sizeof(note),
						"composed U+%04X", cp);
					trace_key(a, code, TL_KEY_PRESS, &m,
						seq, n, note);
				}
				return;
			}
			if (st == TL_COMPOSE_CANCELLED) {
				if (a->keys_mode)
					trace_key(a, code, TL_KEY_PRESS, &m,
						NULL, 0, "compose-cancel");
				return;
			}
		}
	}

	send_key(a, code, TL_KEY_PRESS);
	if (!tl_key_is_modifier(code) && a->repeat_rate > 0) {
		a->repeat_code = code;
		clock_gettime(CLOCK_MONOTONIC, &a->repeat_at);
		ts_add_ms(&a->repeat_at, a->repeat_delay);
	}
}

static void update_hover_link(struct tl_session *a)
{
	uint32_t lid = 0;
	if (a->ptr_inside) {
		int row_cols = 0;
		const struct tl_cell *row = tl_term_abs_row(&a->term,
			view_abs_y(a, a->ptr_cy), &row_cols);
		if (row != NULL && a->ptr_cx >= 0 && a->ptr_cx < row_cols) {
			uint32_t cand = row[a->ptr_cx].link_id;
			if (tl_term_link(&a->term, cand) != NULL)
				lid = cand;
		}
	}
	if (lid != a->term.hover_link_id) {
		a->term.hover_link_id = lid;
		a->term.full_dirty = true;
		a->term.dirty = true;
		a->need_draw = true;
	}
}

static void on_pointer_enter(void *ud, double x, double y)
{
	struct tl_session *a = ud;
	a->ptr_inside = true;
	ptr_to_cell(a, x, y, &a->ptr_cx, &a->ptr_cy);
	update_hover_link(a);
	update_cursor_shape(a);
}

static void on_pointer_leave(void *ud)
{
	struct tl_session *a = ud;
	a->ptr_inside = false;
	update_hover_link(a);
}

static void on_pointer_motion(void *ud, double x, double y)
{
	struct tl_session *a = ud;
	int cx, cy;
	ptr_to_cell(a, x, y, &cx, &cy);
	bool moved = (cx != a->ptr_cx || cy != a->ptr_cy);
	a->ptr_cx = cx;
	a->ptr_cy = cy;
	if (!moved)
		return;
	update_hover_link(a);
	update_cursor_shape(a);
	if (a->sel.dragging) {
		tl_sel_update(&a->sel, &a->term, cx, view_abs_y(a, cy));
		a->term.full_dirty = true;
		a->term.dirty = true;
		draw(a, false);
		return;
	}
	if (mouse_reporting(a)) {
		if (a->term.mouse_mode == TL_MOUSE_ANY ||
		    (a->term.mouse_mode == TL_MOUSE_DRAG &&
		     a->ptr_buttons != 0)) {
			int btn = (a->ptr_buttons & 1) ? 0 :
				  (a->ptr_buttons & 2) ? 1 :
				  (a->ptr_buttons & 4) ? 2 : 3;
			send_mouse(a, 32 + btn, cx, cy, false);
		}
	}
}

static void on_pointer_button(void *ud, uint32_t button, bool pressed,
	uint32_t time)
{
	struct tl_session *a = ud;
	int btn_idx = button == TL_BTN_LEFT ? 0 :
		      button == TL_BTN_MIDDLE ? 1 :
		      button == TL_BTN_RIGHT ? 2 : -1;
	if (btn_idx >= 0) {
		if (pressed)
			a->ptr_buttons |= (1u << btn_idx);
		else
			a->ptr_buttons &= ~(1u << btn_idx);
	}
	if (mouse_reporting(a) && btn_idx >= 0) {
		send_mouse(a, btn_idx, a->ptr_cx, a->ptr_cy, !pressed);
		return;
	}
	if (button == TL_BTN_LEFT) {
		if (pressed) {
			if ((tl_kbd_mods(&a->kbd) & TL_MOD_CTRL) != 0) {
				const char *uri = tl_term_link_uri(&a->term,
					a->ptr_cx, view_abs_y(a, a->ptr_cy));
				if (uri != NULL) {
					(void)tl_action_open_url(uri);
					return;
				}
			}
			if (time - a->last_click_time <= 400)
				a->click_count = (a->click_count % 3) + 1;
			else
				a->click_count = 1;
			a->last_click_time = time;
			enum tl_sel_kind kind =
				a->click_count == 2 ? TL_SEL_WORD :
				a->click_count == 3 ? TL_SEL_LINE : TL_SEL_CHAR;
			tl_sel_start(&a->sel, &a->term, a->ptr_cx,
				view_abs_y(a, a->ptr_cy), kind);
			a->term.full_dirty = true;
			a->term.dirty = true;
			draw(a, false);
		} else if (a->sel.dragging) {
			tl_sel_end(&a->sel);
			size_t len = 0;
			char *text = tl_sel_extract(&a->term, &a->sel, &len);
			if (text != NULL && len > 0)
				tl_window_set_primary(&a->win, text, len);
			free(text);
		}
	} else if (button == TL_BTN_MIDDLE && pressed) {
		tl_term_reset_view(&a->term);
		start_paste(a, true);
	}
}

static void on_pointer_axis(void *ud, uint32_t axis, double value,
	int32_t v120)
{
	struct tl_session *a = ud;
	if (axis != 0)
		return;
	double step = v120 != 0 ? (double)v120 / 120.0 : value / 15.0;
	a->axis_acc += step;
	int notches = (int)a->axis_acc;
	if (notches == 0)
		return;
	a->axis_acc -= (double)notches;
	if (mouse_reporting(a)) {
		int cb = notches < 0 ? 64 : 65;
		int count = notches < 0 ? -notches : notches;
		for (int i = 0; i < count; i++)
			send_mouse(a, cb, a->ptr_cx, a->ptr_cy, false);
		return;
	}
	tl_term_scroll_view(&a->term, -notches * 3);
	draw(a, false);
}

static void on_focus_in(void *ud, const uint32_t *codes, size_t n)
{
	struct tl_session *a = ud;
	tl_display_repeat_info(&a->srv->dpy, &a->repeat_rate, &a->repeat_delay);
	tl_kbd_enter(&a->kbd, codes, n);
	a->focused = true;
	draw(a, false);
}

static void on_focus_out(void *ud)
{
	struct tl_session *a = ud;
	tl_kbd_leave(&a->kbd);
	tl_compose_reset(&a->compose);
	a->repeat_code = 0;
	a->focused = false;
	draw(a, false);
}

static void on_modifiers(void *ud, uint32_t depressed, uint32_t locked)
{
	tl_kbd_modifiers(&((struct tl_session *)ud)->kbd, depressed, locked);
}

static void on_repeat_info(void *ud, int32_t rate, int32_t delay)
{
	struct tl_session *a = ud;
	a->repeat_rate = rate > 1000 ? 1000 : rate;
	a->repeat_delay = delay < 0 ? 0 : delay;
}

static void on_ime_preedit(void *ud, const char *text, int32_t cursor_begin,
	int32_t cursor_end)
{
	struct tl_session *a = ud;
	bool had = a->preedit_text != NULL && a->preedit_text[0] != '\0';
	bool has = text != NULL && text[0] != '\0';
	if (!had && !has)
		return;
	if (a->preedit_y0 >= 0 && a->preedit_y1 >= a->preedit_y0)
		mark_rows_dirty(a, a->preedit_y0, a->preedit_y1);
	mark_rows_dirty(a, a->term.cy, a->term.cy);
	free(a->preedit_text);
	a->preedit_text = NULL;
	if (has) {
		size_t n = strlen(text);
		a->preedit_text = malloc(n + 1);
		if (a->preedit_text != NULL)
			memcpy(a->preedit_text, text, n + 1);
		a->preedit_cbeg = cursor_begin;
		a->preedit_cend = cursor_end;
		tl_compose_reset(&a->compose);
		if (a->keys_mode)
			trace_ime(a, "preedit", text, n, cursor_begin,
				cursor_end);
	} else {
		a->preedit_cbeg = 0;
		a->preedit_cend = 0;
		if (a->keys_mode)
			trace_ime(a, "preedit", NULL, 0, 0, 0);
	}
	a->need_draw = true;
}

static void on_ime_commit(void *ud, const char *text, size_t len)
{
	struct tl_session *a = ud;
	if (text == NULL || len == 0)
		return;
	if (a->search.active) {
		tl_search_push_utf8(&a->search, &a->term, text, len);
		tl_search_sync_view(&a->search, &a->term);
		tl_term_mark_all(&a->term);
		draw(a, false);
		return;
	}
	if (a->urls.active)
		return;
	tl_term_reset_view(&a->term);
	tl_compose_reset(&a->compose);
	(void)tl_pty_queue(&a->pty, text, len);
	if (a->keys_mode)
		trace_ime(a, "commit", text, len, 0, 0);
}

static const struct tl_window_events events = {
	on_configure, on_close, on_frame, on_key, on_focus_in, on_focus_out,
	on_modifiers, on_repeat_info, on_scale,
	on_pointer_enter, on_pointer_leave, on_pointer_motion,
	on_pointer_button, on_pointer_axis,
	on_ime_preedit, on_ime_commit,
};

static void update_sync_timer(struct tl_session *a)
{
	if (a->term.sync_output && !a->sync_armed) {
		clock_gettime(CLOCK_MONOTONIC, &a->sync_deadline);
		ts_add_ms(&a->sync_deadline, TL_SYNC_TIMEOUT_MS);
		a->sync_armed = true;
	} else if (!a->term.sync_output) {
		a->sync_armed = false;
	}
}

/* Read what the program wrote. Returns false at end of file. */
static bool read_pty(struct tl_session *a)
{
	static unsigned char buf[READ_CHUNK];
	for (int i = 0; i < 4; i++) {
		ssize_t n = read(a->pty.master, buf, sizeof(buf));
		if (n > 0) {
			tl_term_feed(&a->term, buf, (size_t)n);
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return true;
		return false;
	}
	return true;
}

static bool reap_session(struct tl_session *a)
{
	int st;
	pid_t r = waitpid(a->pty.pid, &st, WNOHANG);
	if (r != a->pty.pid)
		return false;
	if (WIFEXITED(st))
		a->exit_status = WEXITSTATUS(st);
	else if (WIFSIGNALED(st))
		a->exit_status = 128 + WTERMSIG(st);
	return true;
}

static int session_create(struct tl_server *srv,
	const struct tl_ipc_launch *req, int client_fd,
	char *err, size_t errlen)
{
	if (srv->nsessions >= TL_SERVER_MAX_WINDOWS) {
		snprintf(err, errlen, "too many open windows (max %d)",
			TL_SERVER_MAX_WINDOWS);
		return -1;
	}
	struct tl_session *a = calloc(1, sizeof(*a));
	if (a == NULL) {
		snprintf(err, errlen, "out of memory");
		return -1;
	}
	a->srv = srv;
	a->pty.master = -1;
	a->paste_fd = -1;
	a->osc52_fd = -1;
	a->client_fd = client_fd;
	a->preedit_y0 = a->preedit_y1 = -1;
	a->keys_mode = (req->flags & TL_IPC_F_KEYS_MODE) != 0;
	if (a->keys_mode) {
		a->keys_bar_rows = TL_KEYS_BAR_ROWS;
		snprintf(a->keys_lines[0], sizeof(a->keys_lines[0]),
			"keys: press any key (last %d events here; full log on stderr)",
			TL_KEYS_BAR_ROWS);
		a->keys_count = 1;
		a->buf_bar_dirty[0] = a->buf_bar_dirty[1] = true;
	}

	tl_config_default(&a->cfg);
	if (req->config_path != NULL && req->config_path[0] != '\0') {
		if (tl_config_load_file(&a->cfg, req->config_path) != 0)
			fprintf(stderr, "tam-last: cannot read config %s\n",
				req->config_path);
	} else {
		char def_cfg[512];
		tl_config_default_path(def_cfg, sizeof(def_cfg));
		if (def_cfg[0] != '\0')
			(void)tl_config_load_file(&a->cfg, def_cfg);
	}
	a->pad_logical = a->cfg.pad;

	const char *want_layout =
		(req->layout_name != NULL && req->layout_name[0] != '\0') ?
		req->layout_name : a->cfg.layout;
	a->key_layout = tl_layout_find(want_layout);
	if (a->key_layout == NULL) {
		snprintf(err, errlen, "unknown layout: %s", want_layout);
		free(a);
		return -1;
	}
	tl_kbd_init(&a->kbd);
	tl_kbd_set_layout(&a->kbd, a->key_layout);
	tl_compose_init(&a->compose);
	tl_display_repeat_info(&srv->dpy, &a->repeat_rate, &a->repeat_delay);

	const char *want_font =
		(req->font_path != NULL && req->font_path[0] != '\0') ?
		req->font_path :
		(a->cfg.font[0] != '\0' ? a->cfg.font : NULL);
	const char *resolved_font = tl_font_find(want_font);
	if (resolved_font == NULL) {
		snprintf(err, errlen, "no font found; use --font FILE");
		free(a);
		return -1;
	}
	snprintf(a->font_path, sizeof(a->font_path), "%s", resolved_font);
	a->pt = (req->flags & TL_IPC_F_PT_SET) ? req->pt : a->cfg.size;
	a->scale120 = 120;

	struct tl_font_key fkey;
	build_font_key(a, &fkey);
	a->font = tl_font_cache_acquire(&srv->fonts, &fkey, err, errlen);
	if (a->font == NULL) {
		free(a);
		return -1;
	}
	if (tl_term_init(&a->term, DEFAULT_COLS, DEFAULT_ROWS) != 0) {
		snprintf(err, errlen, "out of memory");
		tl_font_cache_release(&srv->fonts, a->font);
		free(a);
		return -1;
	}
	tl_term_set_palette(&a->term, &a->cfg.pal);
	tl_term_set_osc52_policy(&a->term,
		(req->flags & TL_IPC_F_OSC52_SET) ?
		(enum tl_osc52_policy)req->osc52 : a->cfg.osc52);
	a->notify_policy = (req->flags & TL_IPC_F_NOTIFY_SET) ?
		(enum tl_notify_policy)req->notify : a->cfg.notify;

	a->pad = a->pad_logical;
	int init_rows = DEFAULT_ROWS + (a->keys_mode ? TL_KEYS_BAR_ROWS : 0);
	a->width = a->lw = DEFAULT_COLS * a->font->cell_w + 2 * a->pad_logical;
	a->height = a->lh = init_rows * a->font->cell_h + 2 * a->pad_logical;

	const char *want_term =
		(req->term_name != NULL && req->term_name[0] != '\0') ?
		req->term_name : (a->cfg.term[0] != '\0' ? a->cfg.term : NULL);
	char *const *cmd = (req->argc > 0 && req->argv != NULL) ?
		req->argv : NULL;
	if (tl_pty_spawn_in(&a->pty, cmd, want_term, req->cwd, req->shell,
		    a->term.cols, a->term.rows, a->width, a->height,
		    err, errlen) != 0) {
		tl_term_free(&a->term);
		tl_font_cache_release(&srv->fonts, a->font);
		free(a);
		return -1;
	}
	if (tl_window_open_on(&a->win, &srv->dpy, "tam-last", "tam-last",
		    &events, a) != 0) {
		snprintf(err, errlen, "%s", srv->dpy.err);
		kill(-a->pty.pid, SIGHUP);
		tl_pty_close(&a->pty);
		tl_term_free(&a->term);
		tl_font_cache_release(&srv->fonts, a->font);
		free(a);
		return -1;
	}
	a->pty_open = true;
	srv->sessions[srv->nsessions++] = a;
	return 0;
}

static void session_destroy(struct tl_server *srv, size_t idx)
{
	struct tl_session *a = srv->sessions[idx];
	srv->last_exit_status = a->exit_status;

	if (!a->child_done)
		kill(-a->pty.pid, SIGHUP);
	if (a->client_fd >= 0) {
		uint8_t msg[32];
		size_t n = tl_ipc_encode_exit(a->exit_status, msg, sizeof(msg));
		if (n > 0)
			(void)tl_ipc_write_all(a->client_fd, msg, n);
		close(a->client_fd);
		a->client_fd = -1;
	}
	tl_search_free(&a->search);
	tl_url_end(&a->urls);
	if (a->paste_fd >= 0)
		close(a->paste_fd);
	if (a->osc52_fd >= 0)
		close(a->osc52_fd);
	free(a->osc52_buf);
	free(a->preedit_text);
	free(a->buf_dirty[0]);
	free(a->buf_dirty[1]);
	tl_window_close(&a->win);
	tl_pty_close(&a->pty);
	tl_term_free(&a->term);
	tl_font_cache_release(&srv->fonts, a->font);
	free(a);

	for (size_t j = idx + 1; j < srv->nsessions; j++)
		srv->sessions[j - 1] = srv->sessions[j];
	srv->nsessions--;
}

static void send_ipc_ack(int fd, uint8_t status, const char *msg)
{
	uint8_t buf[512];
	size_t n = tl_ipc_encode_ack(status, msg, buf, sizeof(buf));
	if (n > 0)
		(void)tl_ipc_write_all(fd, buf, n);
}

static void pending_close(struct tl_pending_client *pc)
{
	if (pc->fd >= 0)
		close(pc->fd);
	free(pc->buf);
	pc->fd = -1;
	pc->buf = NULL;
	pc->len = pc->want = 0;
}

static void accept_clients(struct tl_server *srv)
{
	for (;;) {
		int fd = accept(srv->listen_fd, NULL, NULL);
		if (fd < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		fcntl(fd, F_SETFL, O_NONBLOCK);
		/* Only this user's programs may drive the terminal. */
		uid_t peer;
		if (tam_sock_peer_uid(fd, &peer) < 0 || peer != geteuid()) {
			close(fd);
			continue;
		}
		struct tl_pending_client *slot = NULL;
		for (size_t i = 0; i < TL_SERVER_MAX_PENDING; i++) {
			if (srv->pending[i].fd < 0) {
				slot = &srv->pending[i];
				break;
			}
		}
		if (slot == NULL) {
			send_ipc_ack(fd, 1, "server busy");
			close(fd);
			continue;
		}
		slot->buf = malloc(TL_IPC_MAX_MSG);
		if (slot->buf == NULL) {
			send_ipc_ack(fd, 1, "out of memory");
			close(fd);
			continue;
		}
		slot->fd = fd;
		slot->len = 0;
		slot->want = 0;
		clock_gettime(CLOCK_MONOTONIC, &slot->deadline);
		ts_add_ms(&slot->deadline, TL_IPC_HANDSHAKE_TIMEOUT_MS);
	}
}

static void handle_pending_client(struct tl_server *srv,
	struct tl_pending_client *pc)
{
	for (;;) {
		size_t cap = pc->want > 0 ? pc->want : TL_IPC_HDR_LEN;
		if (pc->len >= cap)
			break;
		ssize_t n = read(pc->fd, pc->buf + pc->len, cap - pc->len);
		if (n > 0) {
			pc->len += (size_t)n;
			if (pc->want == 0 && pc->len >= TL_IPC_HDR_LEN) {
				pc->want = tl_ipc_frame_len(pc->buf);
				if (pc->want == 0 || pc->want > TL_IPC_MAX_MSG) {
					send_ipc_ack(pc->fd, 1,
						"malformed request");
					pending_close(pc);
					return;
				}
			}
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		pending_close(pc);
		return;
	}
	if (pc->want == 0 || pc->len < pc->want)
		return;

	struct tl_ipc_launch req;
	if (tl_ipc_decode_launch(pc->buf, pc->len, &req) != 0) {
		send_ipc_ack(pc->fd, 1, "malformed launch request");
		pending_close(pc);
		return;
	}
	bool no_wait = (req.flags & TL_IPC_F_NO_WAIT) != 0;
	int pass_fd = no_wait ? -1 : pc->fd;
	char err[256];
	if (session_create(srv, &req, pass_fd, err, sizeof(err)) != 0) {
		send_ipc_ack(pc->fd, 1, err);
		tl_ipc_launch_free(&req);
		pending_close(pc);
		return;
	}
	send_ipc_ack(pc->fd, 0, NULL);
	tl_ipc_launch_free(&req);
	if (no_wait) {
		pending_close(pc);
	} else {
		/* Ownership of pc->fd transferred to the new session. */
		free(pc->buf);
		pc->fd = -1;
		pc->buf = NULL;
		pc->len = pc->want = 0;
	}
}

static void setup_locale(void)
{
	(void)tam_locale_init();
}

static const char *path_basename(const char *p)
{
	if (p == NULL)
		return "";
	const char *slash = strrchr(p, '/');
	return slash != NULL ? slash + 1 : p;
}

static int run_server_loop(struct tl_server *srv)
{
	bool is_server = srv->listen_fd >= 0;

	/* Fixed upper bound on pollfds: 3 global + pending + 4 per session. */
	enum { MAX_FDS = 3 + TL_SERVER_MAX_PENDING + 4 * TL_SERVER_MAX_WINDOWS };
	struct pollfd fds[MAX_FDS];
	int pty_idx[TL_SERVER_MAX_WINDOWS];
	int paste_idx[TL_SERVER_MAX_WINDOWS];
	int osc52_idx[TL_SERVER_MAX_WINDOWS];
	int cli_idx[TL_SERVER_MAX_WINDOWS];
	int pend_idx[TL_SERVER_MAX_PENDING];

	while (!srv->stop && (is_server || srv->nsessions > 0)) {
		bool pty_more[TL_SERVER_MAX_WINDOWS];
		for (size_t i = 0; i < srv->nsessions; i++) {
			struct tl_session *a = srv->sessions[i];
			handle_term_osc(a);
			update_sync_timer(a);
			if (a->term.reply_len > 0) {
				(void)tl_pty_queue(&a->pty, a->term.reply,
					a->term.reply_len);
				a->term.reply_len = 0;
			}
			update_cursor_shape(a);
			if ((a->term.dirty || a->need_draw) &&
			    !a->term.sync_output)
				draw(a, false);
			int pm = a->pty_open ? tl_pty_flush(&a->pty) : 0;
			if (pm < 0)
				a->pty_open = false;
			pty_more[i] = pm > 0;
		}

		int wl_more = tl_display_flush(&srv->dpy);
		if (wl_more < 0) {
			fprintf(stderr, "tam-last: %s\n", srv->dpy.err);
			break;
		}

		nfds_t nfds = 0;
		fds[nfds++] = (struct pollfd){
			tl_display_fd(&srv->dpy),
			(short)(POLLIN | (wl_more ? POLLOUT : 0)), 0
		};
		fds[nfds++] = (struct pollfd){ sig_pipe[0], POLLIN, 0 };
		int listen_slot = -1;
		if (is_server) {
			listen_slot = (int)nfds;
			fds[nfds++] = (struct pollfd){
				srv->listen_fd, POLLIN, 0
			};
		}
		for (size_t i = 0; i < TL_SERVER_MAX_PENDING; i++) {
			if (srv->pending[i].fd >= 0) {
				pend_idx[i] = (int)nfds;
				fds[nfds++] = (struct pollfd){
					srv->pending[i].fd, POLLIN, 0
				};
			} else {
				pend_idx[i] = -1;
			}
		}
		for (size_t i = 0; i < srv->nsessions; i++) {
			struct tl_session *a = srv->sessions[i];
			if (a->pty_open && a->pty.master >= 0) {
				pty_idx[i] = (int)nfds;
				fds[nfds++] = (struct pollfd){
					a->pty.master,
					(short)(POLLIN | (pty_more[i] ? POLLOUT : 0)),
					0
				};
			} else {
				pty_idx[i] = -1;
			}
			if (a->paste_fd >= 0) {
				paste_idx[i] = (int)nfds;
				fds[nfds++] = (struct pollfd){
					a->paste_fd, POLLIN, 0
				};
			} else {
				paste_idx[i] = -1;
			}
			if (a->osc52_fd >= 0) {
				osc52_idx[i] = (int)nfds;
				fds[nfds++] = (struct pollfd){
					a->osc52_fd, POLLIN, 0
				};
			} else {
				osc52_idx[i] = -1;
			}
			if (a->client_fd >= 0) {
				cli_idx[i] = (int)nfds;
				fds[nfds++] = (struct pollfd){
					a->client_fd, POLLIN, 0
				};
			} else {
				cli_idx[i] = -1;
			}
		}

		int timeout = -1;
		for (size_t i = 0; i < srv->nsessions; i++) {
			struct tl_session *a = srv->sessions[i];
			if (a->repeat_code != 0 && a->focused) {
				int rm = (int)ms_until(&a->repeat_at);
				if (timeout < 0 || rm < timeout)
					timeout = rm;
			}
			if (a->term.sync_output && a->sync_armed) {
				int sm = (int)ms_until(&a->sync_deadline);
				if (timeout < 0 || sm < timeout)
					timeout = sm;
			}
		}
		for (size_t i = 0; i < TL_SERVER_MAX_PENDING; i++) {
			if (srv->pending[i].fd >= 0) {
				int pm = (int)ms_until(&srv->pending[i].deadline);
				if (timeout < 0 || pm < timeout)
					timeout = pm;
			}
		}
		int am = tl_action_next_timeout_ms(&srv->actions);
		if (am >= 0 && (timeout < 0 || am < timeout))
			timeout = am;

		if (poll(fds, nfds, timeout) < 0 && errno != EINTR) {
			perror("tam-last: poll");
			break;
		}

		if (fds[0].revents & POLLIN) {
			if (tl_display_dispatch(&srv->dpy) != 0) {
				fprintf(stderr, "tam-last: %s\n", srv->dpy.err);
				break;
			}
		} else if (fds[0].revents & (POLLERR | POLLHUP)) {
			fprintf(stderr, "tam-last: the compositor went away\n");
			break;
		}

		if (fds[1].revents & POLLIN) {
			char drain[64];
			ssize_t n;
			while ((n = read(sig_pipe[0], drain, sizeof(drain))) > 0) {
				for (ssize_t k = 0; k < n; k++)
					if (drain[k] == 't')
						srv->stop = true;
			}
			for (size_t i = 0; i < srv->nsessions; i++) {
				if (!srv->sessions[i]->child_done)
					srv->sessions[i]->child_done =
						reap_session(srv->sessions[i]);
			}
			tl_action_reap(&srv->actions);
		}

		if (listen_slot >= 0 && (fds[listen_slot].revents & POLLIN))
			accept_clients(srv);

		for (size_t i = 0; i < TL_SERVER_MAX_PENDING; i++) {
			if (srv->pending[i].fd < 0)
				continue;
			if (pend_idx[i] >= 0 &&
			    (fds[pend_idx[i]].revents &
			     (POLLIN | POLLHUP | POLLERR)))
				handle_pending_client(srv, &srv->pending[i]);
			if (srv->pending[i].fd >= 0 &&
			    ms_until(&srv->pending[i].deadline) == 0)
				pending_close(&srv->pending[i]);
		}

		/* Iterate only over sessions that existed when poll() ran. */
		size_t n_polled = srv->nsessions;
		for (size_t i = 0; i < n_polled; i++) {
			struct tl_session *a = srv->sessions[i];
			if (pty_idx[i] >= 0 && a->pty_open &&
			    (fds[pty_idx[i]].revents &
			     (POLLIN | POLLHUP | POLLERR))) {
				a->pty_open = read_pty(a);
				update_sync_timer(a);
			}
			if (paste_idx[i] >= 0 && a->paste_fd >= 0 &&
			    (fds[paste_idx[i]].revents &
			     (POLLIN | POLLHUP | POLLERR)))
				drain_paste(a);
			if (osc52_idx[i] >= 0 && a->osc52_fd >= 0 &&
			    (fds[osc52_idx[i]].revents &
			     (POLLIN | POLLHUP | POLLERR)))
				drain_osc52_read(a);
			if (cli_idx[i] >= 0 && a->client_fd >= 0 &&
			    (fds[cli_idx[i]].revents &
			     (POLLIN | POLLHUP | POLLERR))) {
				/* Waiting client disconnected (e.g. killed). */
				char dummy;
				ssize_t r = read(a->client_fd, &dummy, 1);
				if (r <= 0 && errno != EAGAIN &&
				    errno != EWOULDBLOCK) {
					close(a->client_fd);
					a->client_fd = -1;
				}
			}
		}

		for (size_t i = 0; i < srv->nsessions; i++) {
			struct tl_session *a = srv->sessions[i];
			if (a->repeat_code != 0 && a->focused &&
			    ms_until(&a->repeat_at) == 0) {
				send_key(a, a->repeat_code, TL_KEY_REPEAT);
				ts_add_ms(&a->repeat_at, 1000 / a->repeat_rate);
			}
			if (a->term.sync_output && a->sync_armed &&
			    ms_until(&a->sync_deadline) == 0) {
				a->term.sync_output = false;
				a->sync_armed = false;
			}
		}
		(void)tl_action_check_deadlines(&srv->actions);

		/* Finalize and remove any finished or closed sessions. */
		for (size_t i = 0; i < srv->nsessions; ) {
			struct tl_session *a = srv->sessions[i];
			if (!a->closing && !a->child_done && a->pty_open) {
				i++;
				continue;
			}
			if (!a->closing) {
				if (a->pty_open)
					a->pty_open = read_pty(a);
				if (!a->child_done)
					a->child_done = reap_session(a);
				if (!a->child_done) {
					struct pollfd sp = { sig_pipe[0], POLLIN, 0 };
					if (poll(&sp, 1, 50) > 0)
						a->child_done = reap_session(a);
				}
				if (a->term.dirty)
					draw(a, true);
			}
			session_destroy(srv, i);
			(void)tl_display_flush(&srv->dpy);
		}
	}

	while (srv->nsessions > 0)
		session_destroy(srv, srv->nsessions - 1);
	for (size_t i = 0; i < TL_SERVER_MAX_PENDING; i++)
		pending_close(&srv->pending[i]);
	tl_actions_kill_all(&srv->actions);
	if (srv->listen_fd >= 0) {
		close(srv->listen_fd);
		srv->listen_fd = -1;
		if (srv->socket_path[0] != '\0')
			unlink(srv->socket_path);
	}
	tl_display_close(&srv->dpy);
	tl_font_cache_free(&srv->fonts);
	return is_server ? 0 : srv->last_exit_status;
}

int main(int argc, char **argv)
{
	bool server_mode = false;
	bool client_mode = strcmp(path_basename(argv[0]),
		"tam-last-client") == 0;
	bool no_wait = false;
	const char *socket_path = NULL;
	const char *config_path = NULL;
	const char *font_path = NULL;
	const char *layout_name = NULL;
	const char *term_name = NULL;
	enum tl_osc52_policy osc52_cli = TL_OSC52_WRITE;
	bool osc52_set = false;
	enum tl_notify_policy notify_cli = TL_NOTIFY_UNFOCUSED;
	bool notify_set = false;
	float pt = 0;
	bool pt_set = false;
	bool keys_mode = false;
	char **cmd = NULL;
	size_t cmd_argc = 0;

	for (int i = 1; i < argc; i++) {
		const char *s = argv[i];
		if (strcmp(s, "-e") == 0) {
			if (i + 1 >= argc) {
				usage(stderr);
				return 2;
			}
			cmd = argv + i + 1;
			cmd_argc = (size_t)(argc - i - 1);
			break;
		} else if (strcmp(s, "--server") == 0) {
			server_mode = true;
			client_mode = false;
		} else if (strcmp(s, "--client") == 0) {
			client_mode = true;
		} else if ((strcmp(s, "-S") == 0 || strcmp(s, "--socket") == 0) &&
			   i + 1 < argc) {
			socket_path = argv[++i];
		} else if (strcmp(s, "-N") == 0 || strcmp(s, "--no-wait") == 0) {
			no_wait = true;
		} else if ((strcmp(s, "-c") == 0 || strcmp(s, "--config") == 0) &&
			   i + 1 < argc) {
			config_path = argv[++i];
		} else if ((strcmp(s, "-f") == 0 || strcmp(s, "--font") == 0) &&
			   i + 1 < argc) {
			font_path = argv[++i];
		} else if ((strcmp(s, "-s") == 0 || strcmp(s, "--size") == 0) &&
			   i + 1 < argc) {
			char *end;
			pt = strtof(argv[++i], &end);
			if (*end != '\0' || pt < 3 || pt > 200) {
				fprintf(stderr, "tam-last: bad size: %s\n",
					argv[i]);
				return 2;
			}
			pt_set = true;
		} else if ((strcmp(s, "-l") == 0 || strcmp(s, "--layout") == 0) &&
			   i + 1 < argc) {
			layout_name = argv[++i];
		} else if (strcmp(s, "--term") == 0 && i + 1 < argc) {
			term_name = argv[++i];
		} else if (strcmp(s, "--osc52") == 0 && i + 1 < argc) {
			if (tl_osc52_policy_parse(argv[++i], &osc52_cli) != 0) {
				fprintf(stderr,
					"tam-last: bad osc52 policy: %s\n",
					argv[i]);
				return 2;
			}
			osc52_set = true;
		} else if (strcmp(s, "--notify") == 0 && i + 1 < argc) {
			if (tl_notify_policy_parse(argv[++i], &notify_cli) != 0) {
				fprintf(stderr,
					"tam-last: bad notify policy: %s\n",
					argv[i]);
				return 2;
			}
			notify_set = true;
		} else if (strcmp(s, "--keys") == 0) {
			keys_mode = true;
		} else if (strcmp(s, "-v") == 0 || strcmp(s, "--version") == 0) {
			printf("tam-last %s\n", TL_VERSION);
			return 0;
		} else if (strcmp(s, "-h") == 0 || strcmp(s, "--help") == 0) {
			usage(stdout);
			return 0;
		} else {
			fprintf(stderr, "tam-last: unknown option: %s\n", s);
			usage(stderr);
			return 2;
		}
	}

	if (server_mode && client_mode) {
		fprintf(stderr,
			"tam-last: --server and --client cannot be combined\n");
		return 2;
	}
	if (layout_name != NULL && strcmp(layout_name, "list") == 0) {
		list_layouts(stdout);
		return 0;
	}

	char cwd_buf[4096];
	cwd_buf[0] = '\0';
	(void)getcwd(cwd_buf, sizeof(cwd_buf));

	struct tl_ipc_launch req;
	memset(&req, 0, sizeof(req));
	if (pt_set) {
		req.flags |= TL_IPC_F_PT_SET;
		req.pt = pt;
	}
	if (osc52_set) {
		req.flags |= TL_IPC_F_OSC52_SET;
		req.osc52 = (uint8_t)osc52_cli;
	}
	if (notify_set) {
		req.flags |= TL_IPC_F_NOTIFY_SET;
		req.notify = (uint8_t)notify_cli;
	}
	if (keys_mode)
		req.flags |= TL_IPC_F_KEYS_MODE;
	if (no_wait)
		req.flags |= TL_IPC_F_NO_WAIT;
	req.cwd = cwd_buf[0] != '\0' ? cwd_buf : NULL;
	req.shell = getenv("SHELL");
	req.config_path = (char *)config_path;
	req.font_path = (char *)font_path;
	req.layout_name = (char *)layout_name;
	req.term_name = (char *)term_name;
	req.argv = cmd;
	req.argc = cmd_argc;

	if (client_mode) {
		signal(SIGPIPE, SIG_IGN);
		char sock[256];
		char err[256];
		if (socket_path != NULL && socket_path[0] != '\0') {
			snprintf(sock, sizeof(sock), "%s", socket_path);
		} else if (tl_ipc_default_socket(sock, sizeof(sock), err,
			   sizeof(err)) != 0) {
			fprintf(stderr, "tam-last: %s\n", err);
			return 1;
		}
		return tl_ipc_run_client(sock, &req);
	}

	setup_locale();
	if (pipe(sig_pipe) != 0) {
		perror("tam-last: pipe");
		return 1;
	}
	for (int i = 0; i < 2; i++) {
		fcntl(sig_pipe[i], F_SETFD, FD_CLOEXEC);
		fcntl(sig_pipe[i], F_SETFL, O_NONBLOCK);
	}
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_sigchld;
	sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
	sigaction(SIGCHLD, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	static struct tl_server srv;
	memset(&srv, 0, sizeof(srv));
	srv.listen_fd = -1;
	for (size_t i = 0; i < TL_SERVER_MAX_PENDING; i++)
		srv.pending[i].fd = -1;
	tl_font_cache_init(&srv.fonts);
	tl_actions_init(&srv.actions);

	if (tl_display_open(&srv.dpy) != 0) {
		fprintf(stderr, "tam-last: %s\n", srv.dpy.err);
		return 1;
	}

	char err[300];
	if (server_mode) {
		if (socket_path != NULL && socket_path[0] != '\0') {
			snprintf(srv.socket_path, sizeof(srv.socket_path),
				"%s", socket_path);
		} else if (tl_ipc_default_socket(srv.socket_path,
			   sizeof(srv.socket_path), err, sizeof(err)) != 0) {
			fprintf(stderr, "tam-last: %s\n", err);
			tl_display_close(&srv.dpy);
			return 1;
		}
		srv.listen_fd = tl_ipc_listen(srv.socket_path, err,
			sizeof(err));
		if (srv.listen_fd < 0) {
			fprintf(stderr, "tam-last: %s\n", err);
			tl_display_close(&srv.dpy);
			return 1;
		}
		struct sigaction st;
		memset(&st, 0, sizeof(st));
		st.sa_handler = on_sigterm;
		sigaction(SIGINT, &st, NULL);
		sigaction(SIGTERM, &st, NULL);
	} else {
		if (session_create(&srv, &req, -1, err, sizeof(err)) != 0) {
			fprintf(stderr, "tam-last: %s\n", err);
			tl_display_close(&srv.dpy);
			tl_font_cache_free(&srv.fonts);
			return 1;
		}
	}

	return run_server_loop(&srv);
}
