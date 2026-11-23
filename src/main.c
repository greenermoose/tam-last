/* tam-last: start-up, options, and the one event loop.
 *
 * The loop waits in poll() on three descriptors: the compositor's socket,
 * the pty, and a pipe the SIGCHLD handler writes to. It has no timeout
 * except while a key repeats, so an idle terminal uses no CPU. */
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "tam-last/config.h"
#include "tam-last/font.h"
#include "tam-last/keys.h"
#include "tam-last/osc.h"
#include "tam-last/proto.h"
#include "tam-last/pty.h"
#include "tam-last/render.h"
#include "tam-last/select.h"
#include "tam-last/term.h"
#include "tam-last/window.h"

#ifndef TL_VERSION
#define TL_VERSION "unknown"
#endif

#define PAD 2
#define DEFAULT_COLS 80
#define DEFAULT_ROWS 24
#define READ_CHUNK 65536
#define TL_BTN_LEFT 0x110u
#define TL_BTN_RIGHT 0x111u
#define TL_BTN_MIDDLE 0x112u
#define TL_KEYS_BAR_ROWS 4
#define TL_OSC52_READ_MAX 65536u
#define TL_SYNC_TIMEOUT_MS 1000

struct app {
	struct tl_window win;
	struct tl_term term;
	struct tl_font font;
	struct tl_pty pty;
	struct tl_kbd kbd;
	const struct tl_layout *key_layout;
	struct tl_compose compose;
	struct tl_config cfg;
	const char *font_path;
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
};

static int sigchld_pipe[2] = { -1, -1 };

static void on_sigchld(int sig)
{
	(void)sig;
	int saved = errno;
	(void)!write(sigchld_pipe[1], "c", 1);
	errno = saved;
}

static void usage(FILE *out)
{
	fputs("usage: tam-last [-c CONFIG] [-f FONT] [-s POINTS] [-l LAYOUT] [--term NAME] [--osc52 POLICY] [--keys] [-e COMMAND [ARG...]]\n"
	      "\n"
	      "  -c, --config FILE  configuration file (default: ~/.config/tam-last/tam-last.ini)\n"
	      "  -f, --font FILE    a TrueType font file (default: $TAM_LAST_FONT,\n"
	      "                     then JetBrains Mono, DejaVu Sans Mono, ...)\n"
	      "  -s, --size POINTS  font size (default 9)\n"
	      "  -l, --layout NAME  keyboard layout (default: us; 'list' prints available)\n"
	      "      --term NAME    override $TERM (default: tam-last if installed, else xterm-256color)\n"
	      "      --osc52 POLICY clipboard policy: write/copy (default), disabled/none,\n"
	      "                     read/paste, both/copy-paste\n"
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

static void sync_dirty(struct app *a)
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

static void mark_rows_dirty(struct app *a, int y0, int y1)
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

static void update_ime_cursor_rect(struct app *a)
{
	if (!tl_window_has_text_input(&a->win))
		return;
	int dx = a->pad + a->term.cx * a->font.cell_w;
	int dy = a->pad + a->term.cy * a->font.cell_h;
	int dw = a->font.cell_w;
	int dh = a->font.cell_h;
	int32_t lx = (int32_t)(((long)dx * 120) / a->scale120);
	int32_t ly = (int32_t)(((long)dy * 120) / a->scale120);
	int32_t lw = (int32_t)(((long)dw * 120 + a->scale120 - 1) / a->scale120);
	int32_t lh = (int32_t)(((long)dh * 120 + a->scale120 - 1) / a->scale120);
	tl_window_set_cursor_rect(&a->win, lx, ly, lw, lh);
}

static void draw(struct app *a, bool force)
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
	bool show_preedit = a->preedit_text != NULL && a->preedit_text[0] != '\0' &&
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
	tl_render_rows(&a->term, &a->font, &a->term.pal, b->pixels, b->width,
		b->height, a->pad, a->focused, &a->sel, a->buf_dirty[idx], full);
	if (show_preedit) {
		struct tl_preedit pe = {
			a->preedit_text, a->preedit_cbeg, a->preedit_cend
		};
		int last_y = tl_render_preedit(&a->term, &a->font, &a->term.pal,
			b->pixels, b->width, b->height, a->pad, &pe);
		if (last_y >= 0) {
			a->preedit_y0 = a->term.cy;
			a->preedit_y1 = last_y;
			if (!full && a->buf_dirty[idx] != NULL) {
				for (int y = a->preedit_y0; y <= last_y && y < a->buf_dirty_rows; y++)
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
			tl_render_bar(&a->font, &a->term.pal, b->pixels, b->width,
				b->height, a->pad, a->term.rows + r,
				a->term.cols, text);
		}
		a->buf_bar_dirty[idx] = false;
		bar_drawn = true;
	}

	struct tl_rect rects[64];
	size_t nrects = 0;
	if (!full) {
		int cw = a->font.cell_w, ch = a->font.cell_h;
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
				nrects = 0; /* too many bands: damage whole buffer */
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
		memset(a->buf_dirty[idx], 0, (size_t)a->buf_dirty_rows * sizeof(bool));
	a->need_draw = false;
	a->term.dirty = false;
}

static void trace_append_line(struct app *a, const char *line)
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

static void trace_key(struct app *a, uint32_t code, enum tl_key_event ev,
	const struct tl_key_modes *m, const char *seq, size_t n,
	const char *note)
{
	char line[512];
	tl_key_trace_format(line, sizeof(line), code, tl_kbd_mods(&a->kbd),
		ev, m, seq, n, note);
	trace_append_line(a, line);
}

static void trace_ime(struct app *a, const char *kind, const char *text,
	size_t len, int32_t cbeg, int32_t cend)
{
	char line[512];
	tl_key_trace_ime(line, sizeof(line), kind, text, len, cbeg, cend);
	trace_append_line(a, line);
}

static struct tl_key_modes key_modes(const struct app *a)
{
	struct tl_key_modes m = {
		a->term.app_cursor, a->term.app_keypad, a->kbd.num, a->kbd.caps,
		tl_term_kitty_kbd_flags(&a->term), a->key_layout,
	};
	return m;
}

static void trace_shortcut(struct app *a, uint32_t code, const char *note)
{
	a->shortcut_code = code;
	if (!a->keys_mode)
		return;
	struct tl_key_modes m = key_modes(a);
	trace_key(a, code, TL_KEY_PRESS, &m, NULL, 0, note);
}

static void send_key(struct app *a, uint32_t code, enum tl_key_event ev)
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
static int to_device(const struct app *a, int v)
{
	return (int)(((long)v * a->scale120 + 60) / 120);
}

static int to_logical(const struct app *a, int v)
{
	return (int)(((long)v * 120 + a->scale120 - 1) / a->scale120);
}

/* Fit the grid to the window's logical size at the current scale. */
static void layout(struct app *a)
{
	a->pad = to_device(a, a->pad_logical);
	a->width = to_device(a, a->lw);
	a->height = to_device(a, a->lh);
	int cols = (a->width - 2 * a->pad) / a->font.cell_w;
	int total_rows = (a->height - 2 * a->pad) / a->font.cell_h;
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
	struct app *a = ud;
	if (width <= 0 || height <= 0) {
		/* Ours to choose: keep the current grid. */
		int want_rows = a->term.rows +
			(a->keys_mode ? TL_KEYS_BAR_ROWS : 0);
		width = to_logical(a, a->term.cols * a->font.cell_w +
			2 * to_device(a, a->pad_logical));
		height = to_logical(a, want_rows * a->font.cell_h +
			2 * to_device(a, a->pad_logical));
	}
	a->lw = width;
	a->lh = height;
	layout(a);
	/* A configure must be answered with a commit at the new size. */
	draw(a, true);
}

/* The font at the size it has on this output: pt points at 96 dpi times
 * the scale, with bold/italic sibling faces and fallback fonts. */
static int open_font(struct app *a, struct tl_font *f, char *err,
	size_t errlen)
{
	int dpi = (int)((96L * a->scale120 + 60) / 120);
	if (tl_font_open(f, a->font_path, a->pt, dpi, err, errlen) != 0)
		return -1;
	if (a->cfg.font_bold[0] != '\0')
		(void)tl_font_load_face(f, TL_STYLE_BOLD, a->cfg.font_bold);
	if (a->cfg.font_italic[0] != '\0')
		(void)tl_font_load_face(f, TL_STYLE_ITALIC, a->cfg.font_italic);
	if (a->cfg.font_bold_italic[0] != '\0')
		(void)tl_font_load_face(f, TL_STYLE_BOLD_ITALIC,
			a->cfg.font_bold_italic);
	tl_font_load_siblings(f);
	if (a->cfg.font_fallback[0] != '\0')
		(void)tl_font_add_fallback(f, a->cfg.font_fallback);
	tl_font_load_default_fallbacks(f);
	return 0;
}

static void on_scale(void *ud, uint32_t scale120)
{
	struct app *a = ud;
	if (scale120 == a->scale120)
		return;
	uint32_t old = a->scale120;
	a->scale120 = scale120;
	struct tl_font f;
	char err[300];
	if (open_font(a, &f, err, sizeof(err)) != 0) {
		fprintf(stderr, "tam-last: %s\n", err);
		a->scale120 = old;
		return;
	}
	tl_font_close(&a->font);
	a->font = f;
	layout(a);
	a->term.full_dirty = true;
	a->term.dirty = true;
	draw(a, true);
}

static void on_close(void *ud)
{
	((struct app *)ud)->closing = true;
}

static void on_frame(void *ud)
{
	struct app *a = ud;
	if (a->need_draw || a->term.dirty)
		draw(a, false);
}

static void update_cursor_shape(struct app *a)
{
	bool mouse_app = a->term.mouse_mode != TL_MOUSE_NONE &&
		!(tl_kbd_mods(&a->kbd) & TL_MOD_SHIFT);
	uint32_t shape = mouse_app ? TL_CURSOR_SHAPE_DEFAULT :
		a->term.hover_link_id != 0 ? TL_CURSOR_SHAPE_POINTER :
		TL_CURSOR_SHAPE_TEXT;
	tl_window_set_cursor(&a->win, shape);
}

static void send_osc52_reply(struct app *a, const char *target, bool st,
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

static void finish_osc52_read(struct app *a)
{
	send_osc52_reply(a, a->osc52_target, a->osc52_st,
		a->osc52_buf != NULL ? a->osc52_buf : "", a->osc52_len);
	if (a->osc52_fd >= 0) {
		close(a->osc52_fd);
		a->osc52_fd = -1;
	}
	a->osc52_len = 0;
}

static void drain_osc52_read(struct app *a)
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
					size_t ncap = a->osc52_cap == 0 ? 4096 : a->osc52_cap * 2;
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
					memcpy(a->osc52_buf + a->osc52_len, buf, take);
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

static void handle_term_osc(struct app *a)
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
			const char *local = a->term.osc52_read_prim ?
				a->win.prim_text : a->win.clip_text;
			size_t local_len = a->term.osc52_read_prim ?
				a->win.prim_len : a->win.clip_len;
			send_osc52_reply(a, a->term.osc52_read_target,
				a->term.osc52_read_st,
				local != NULL ? local : "",
				local != NULL ? local_len : 0);
		}
	}
}

static void start_paste(struct app *a, bool primary)
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

static void drain_paste(struct app *a)
{
	char buf[4096];
	for (;;) {
		ssize_t n = read(a->paste_fd, buf, sizeof(buf));
		if (n > 0) {
			/* Strip ESC when bracketed so pasted data cannot close \033[201~. */
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
		/* EOF or error: finish paste. */
		if (a->paste_bracketed)
			(void)tl_pty_queue(&a->pty, "\033[201~", 6);
		a->paste_bracketed = false;
		close(a->paste_fd);
		a->paste_fd = -1;
		return;
	}
}

static bool mouse_reporting(const struct app *a)
{
	return a->term.mouse_mode != TL_MOUSE_NONE &&
		!(tl_kbd_mods(&a->kbd) & TL_MOD_SHIFT);
}

static void ptr_to_cell(const struct app *a, double lx, double ly,
	int *cx, int *cy)
{
	double dx = lx * (double)a->scale120 / 120.0;
	double dy = ly * (double)a->scale120 / 120.0;
	int x = (int)((dx - a->pad) / a->font.cell_w);
	int y = (int)((dy - a->pad) / a->font.cell_h);
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

static int mouse_mod_bits(const struct app *a)
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

static void send_mouse(struct app *a, int cb, int cx, int cy, bool release)
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

static int view_abs_y(const struct app *a, int cy)
{
	int sb = a->term.alt_screen ? 0 : a->term.sb_len;
	int vo = a->term.alt_screen ? 0 : a->term.view_offset;
	return sb - vo + cy;
}

static void on_key(void *ud, uint32_t code, bool pressed)
{
	struct app *a = ud;
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
	int page = a->term.rows / 2 > 0 ? a->term.rows / 2 : 1;
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
	if (code == TL_KEY_C && mods == (TL_MOD_CTRL | TL_MOD_SHIFT)) {
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
		trace_shortcut(a, code, "paste-primary");
		tl_term_reset_view(&a->term);
		start_paste(a, true);
		return;
	}
	if (!tl_key_is_modifier(code))
		tl_term_reset_view(&a->term);

	/* Compose and dead-key handling (when no Ctrl/Alt/Meta is held). */
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

static void update_hover_link(struct app *a)
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
	struct app *a = ud;
	a->ptr_inside = true;
	ptr_to_cell(a, x, y, &a->ptr_cx, &a->ptr_cy);
	update_hover_link(a);
	update_cursor_shape(a);
}

static void on_pointer_leave(void *ud)
{
	struct app *a = ud;
	a->ptr_inside = false;
	update_hover_link(a);
}

static void on_pointer_motion(void *ud, double x, double y)
{
	struct app *a = ud;
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
		    (a->term.mouse_mode == TL_MOUSE_DRAG && a->ptr_buttons != 0)) {
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
	struct app *a = ud;
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
			if (time - a->last_click_time <= 400)
				a->click_count = (a->click_count % 3) + 1;
			else
				a->click_count = 1;
			a->last_click_time = time;
			enum tl_sel_kind kind = a->click_count == 2 ? TL_SEL_WORD :
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
	struct app *a = ud;
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
	/* Negative axis is scroll up (into scrollback). */
	tl_term_scroll_view(&a->term, -notches * 3);
	draw(a, false);
}

static void on_focus_in(void *ud, const uint32_t *codes, size_t n)
{
	struct app *a = ud;
	tl_kbd_enter(&a->kbd, codes, n);
	a->focused = true;
	draw(a, false);
}

static void on_focus_out(void *ud)
{
	struct app *a = ud;
	tl_kbd_leave(&a->kbd);
	tl_compose_reset(&a->compose);
	a->repeat_code = 0;
	a->focused = false;
	draw(a, false);
}

static void on_modifiers(void *ud, uint32_t depressed, uint32_t locked)
{
	(void)depressed;
	tl_kbd_locks(&((struct app *)ud)->kbd, locked);
}

static void on_repeat_info(void *ud, int32_t rate, int32_t delay)
{
	struct app *a = ud;
	a->repeat_rate = rate > 1000 ? 1000 : rate;
	a->repeat_delay = delay < 0 ? 0 : delay;
}

static void on_ime_preedit(void *ud, const char *text, int32_t cursor_begin,
	int32_t cursor_end)
{
	struct app *a = ud;
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
	struct app *a = ud;
	if (text == NULL || len == 0)
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

static void update_sync_timer(struct app *a)
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
static bool read_pty(struct app *a)
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
		return false; /* 0, or EIO once the last slave fd closed */
	}
	return true;
}

/* Reap the child; returns true once it has exited. */
static bool reap(struct app *a, int *status)
{
	int st;
	pid_t r = waitpid(a->pty.pid, &st, WNOHANG);
	if (r != a->pty.pid)
		return false;
	if (WIFEXITED(st))
		*status = WEXITSTATUS(st);
	else if (WIFSIGNALED(st))
		*status = 128 + WTERMSIG(st);
	return true;
}

static void setup_locale(void)
{
	/* wcwidth needs a UTF-8 locale for character widths. */
	if (setlocale(LC_CTYPE, "") == NULL ||
	    strcmp(nl_langinfo(CODESET), "UTF-8") != 0)
		setlocale(LC_CTYPE, "C.UTF-8");
}

int main(int argc, char **argv)
{
	const char *config_path = NULL;
	const char *font_path = NULL;
	const char *layout_name = NULL;
	const char *term_name = NULL;
	enum tl_osc52_policy osc52_cli = TL_OSC52_WRITE;
	bool osc52_set = false;
	float pt = 0;
	bool pt_set = false;
	bool keys_mode = false;
	char **cmd = NULL;

	for (int i = 1; i < argc; i++) {
		const char *s = argv[i];
		if (strcmp(s, "-e") == 0) {
			if (i + 1 >= argc) {
				usage(stderr);
				return 2;
			}
			cmd = argv + i + 1;
			break;
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
				fprintf(stderr, "tam-last: bad size: %s\n", argv[i]);
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
				fprintf(stderr, "tam-last: bad osc52 policy: %s\n",
					argv[i]);
				return 2;
			}
			osc52_set = true;
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

	if (layout_name != NULL && strcmp(layout_name, "list") == 0) {
		list_layouts(stdout);
		return 0;
	}

	setup_locale();
	static struct app a;
	a.paste_fd = -1;
	a.osc52_fd = -1;
	a.preedit_y0 = a.preedit_y1 = -1;
	a.keys_mode = keys_mode;
	if (a.keys_mode) {
		a.keys_bar_rows = TL_KEYS_BAR_ROWS;
		snprintf(a.keys_lines[0], sizeof(a.keys_lines[0]),
			"keys: press any key (last %d events here; full log on stderr)",
			TL_KEYS_BAR_ROWS);
		a.keys_count = 1;
		a.buf_bar_dirty[0] = a.buf_bar_dirty[1] = true;
	}
	char err[300];
	tl_config_default(&a.cfg);
	if (config_path != NULL) {
		if (tl_config_load_file(&a.cfg, config_path) != 0)
			fprintf(stderr, "tam-last: cannot read config %s\n",
				config_path);
	} else {
		char def_cfg[512];
		tl_config_default_path(def_cfg, sizeof(def_cfg));
		if (def_cfg[0] != '\0')
			(void)tl_config_load_file(&a.cfg, def_cfg);
	}
	a.pad_logical = a.cfg.pad;
	const char *want_layout = layout_name != NULL ? layout_name : a.cfg.layout;
	a.key_layout = tl_layout_find(want_layout);
	if (a.key_layout == NULL) {
		fprintf(stderr, "tam-last: unknown layout: %s\navailable layouts:\n",
			want_layout);
		list_layouts(stderr);
		return 2;
	}
	tl_kbd_init(&a.kbd);
	tl_kbd_set_layout(&a.kbd, a.key_layout);
	tl_compose_init(&a.compose);

	const char *want_font = font_path != NULL ? font_path :
		(a.cfg.font[0] != '\0' ? a.cfg.font : NULL);
	a.font_path = tl_font_find(want_font);
	a.pt = pt_set ? pt : a.cfg.size;
	a.scale120 = 120; /* until the compositor says otherwise */
	if (a.font_path == NULL) {
		fprintf(stderr, "tam-last: no font found; use --font FILE\n");
		return 1;
	}
	if (open_font(&a, &a.font, err, sizeof(err)) != 0) {
		fprintf(stderr, "tam-last: %s\n", err);
		return 1;
	}
	if (tl_term_init(&a.term, DEFAULT_COLS, DEFAULT_ROWS) != 0) {
		fprintf(stderr, "tam-last: out of memory\n");
		return 1;
	}
	tl_term_set_palette(&a.term, &a.cfg.pal);
	tl_term_set_osc52_policy(&a.term, osc52_set ? osc52_cli : a.cfg.osc52);
	a.pad = a.pad_logical;
	int init_rows = DEFAULT_ROWS + (a.keys_mode ? TL_KEYS_BAR_ROWS : 0);
	a.width = a.lw = DEFAULT_COLS * a.font.cell_w + 2 * a.pad_logical;
	a.height = a.lh = init_rows * a.font.cell_h + 2 * a.pad_logical;

	if (pipe(sigchld_pipe) != 0) {
		perror("tam-last: pipe");
		return 1;
	}
	for (int i = 0; i < 2; i++) {
		fcntl(sigchld_pipe[i], F_SETFD, FD_CLOEXEC);
		fcntl(sigchld_pipe[i], F_SETFL, O_NONBLOCK);
	}
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_sigchld;
	sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
	sigaction(SIGCHLD, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	/* The program starts first, so its first output is ready to draw. */
	const char *want_term = term_name != NULL ? term_name :
		(a.cfg.term[0] != '\0' ? a.cfg.term : NULL);
	if (tl_pty_spawn(&a.pty, cmd, want_term, a.term.cols, a.term.rows,
		    a.width, a.height, err, sizeof(err)) != 0) {
		fprintf(stderr, "tam-last: %s\n", err);
		return 1;
	}
	if (tl_window_open(&a.win, "tam-last", "tam-last", &events, &a) != 0) {
		fprintf(stderr, "tam-last: %s\n", a.win.err);
		kill(-a.pty.pid, SIGHUP);
		return 1;
	}

	int status = 0;
	bool pty_open = true, child_done = false;
	while (!a.closing) {
		handle_term_osc(&a);
		update_sync_timer(&a);
		if (a.term.reply_len > 0) {
			(void)tl_pty_queue(&a.pty, a.term.reply, a.term.reply_len);
			a.term.reply_len = 0;
		}
		update_cursor_shape(&a);
		if ((a.term.dirty || a.need_draw) && !a.term.sync_output)
			draw(&a, false);

		int pty_more = pty_open ? tl_pty_flush(&a.pty) : 0;
		int wl_more = tl_window_flush(&a.win);
		if (wl_more < 0 || pty_more < 0) {
			if (wl_more < 0)
				fprintf(stderr, "tam-last: %s\n", a.win.err);
			break;
		}

		struct pollfd fds[5] = {
			{ tl_window_fd(&a.win), POLLIN | (wl_more ? POLLOUT : 0), 0 },
			{ pty_open ? a.pty.master : -1,
			  POLLIN | (pty_more ? POLLOUT : 0), 0 },
			{ sigchld_pipe[0], POLLIN, 0 },
			{ a.paste_fd, POLLIN, 0 },
			{ a.osc52_fd, POLLIN, 0 },
		};
		int timeout = -1;
		if (a.repeat_code != 0 && a.focused)
			timeout = (int)ms_until(&a.repeat_at);
		if (a.term.sync_output && a.sync_armed) {
			int sm = (int)ms_until(&a.sync_deadline);
			if (timeout < 0 || sm < timeout)
				timeout = sm;
		}

		if (poll(fds, 5, timeout) < 0 && errno != EINTR) {
			perror("tam-last: poll");
			break;
		}

		if (fds[0].revents & POLLIN) {
			if (tl_window_dispatch(&a.win) != 0) {
				fprintf(stderr, "tam-last: %s\n", a.win.err);
				break;
			}
		} else if (fds[0].revents & (POLLERR | POLLHUP)) {
			fprintf(stderr, "tam-last: the compositor went away\n");
			break;
		}

		if (pty_open && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
			pty_open = read_pty(&a);
			update_sync_timer(&a);
		}

		if (fds[2].revents & POLLIN) {
			char drain[64];
			while (read(sigchld_pipe[0], drain, sizeof(drain)) > 0)
				;
			if (!child_done)
				child_done = reap(&a, &status);
		}

		if (a.paste_fd >= 0 &&
		    (fds[3].revents & (POLLIN | POLLHUP | POLLERR)))
			drain_paste(&a);

		if (a.osc52_fd >= 0 &&
		    (fds[4].revents & (POLLIN | POLLHUP | POLLERR)))
			drain_osc52_read(&a);

		if (a.repeat_code != 0 && a.focused &&
		    ms_until(&a.repeat_at) == 0) {
			send_key(&a, a.repeat_code, TL_KEY_REPEAT);
			ts_add_ms(&a.repeat_at, 1000 / a.repeat_rate);
		}

		if (a.term.sync_output && a.sync_armed &&
		    ms_until(&a.sync_deadline) == 0) {
			a.term.sync_output = false;
			a.sync_armed = false;
		}

		/* Done when the program has exited, or nothing can write to
		 * the terminal any more. */
		if (child_done || !pty_open) {
			if (pty_open)
				pty_open = read_pty(&a); /* its last words */
			if (!child_done)
				child_done = reap(&a, &status);
			if (!child_done) {
				struct pollfd sp = { sigchld_pipe[0], POLLIN, 0 };
				if (poll(&sp, 1, 50) > 0)
					child_done = reap(&a, &status);
			}
			if (a.term.dirty)
				draw(&a, true);
			(void)tl_window_flush(&a.win);
			break;
		}
	}

	if (!child_done)
		kill(-a.pty.pid, SIGHUP);
	if (a.paste_fd >= 0)
		close(a.paste_fd);
	if (a.osc52_fd >= 0)
		close(a.osc52_fd);
	free(a.osc52_buf);
	free(a.preedit_text);
	free(a.buf_dirty[0]);
	free(a.buf_dirty[1]);
	tl_window_close(&a.win);
	tl_pty_close(&a.pty);
	tl_term_free(&a.term);
	tl_font_close(&a.font);
	return status;
}
