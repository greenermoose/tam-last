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

#include "tam-last/font.h"
#include "tam-last/keys.h"
#include "tam-last/pty.h"
#include "tam-last/render.h"
#include "tam-last/term.h"
#include "tam-last/window.h"

#ifndef TL_VERSION
#define TL_VERSION "unknown"
#endif

#define PAD 2
#define DEFAULT_COLS 80
#define DEFAULT_ROWS 24
#define READ_CHUNK 65536

struct app {
	struct tl_window win;
	struct tl_term term;
	struct tl_font font;
	struct tl_pty pty;
	struct tl_kbd kbd;
	struct tl_palette pal;
	int width, height;
	bool focused;
	bool need_draw;
	bool closing;
	int32_t repeat_rate, repeat_delay;
	uint32_t repeat_code;
	struct timespec repeat_at;
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
	fputs("usage: tam-last [-f FONT] [-s POINTS] [-e COMMAND [ARG...]]\n"
	      "\n"
	      "  -f, --font FILE    a TrueType font file (default: $TAM_LAST_FONT,\n"
	      "                     then JetBrains Mono, DejaVu Sans Mono, ...)\n"
	      "  -s, --size POINTS  font size (default 9)\n"
	      "  -e COMMAND ...     run COMMAND instead of $SHELL\n"
	      "  -v, --version      print the version\n"
	      "  -h, --help         this help\n", out);
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

static void draw(struct app *a, bool force)
{
	if (!a->win.configured)
		return;
	if (tl_window_frame_pending(&a->win) && !force) {
		a->need_draw = true;
		return;
	}
	struct tl_buffer *b = tl_window_buffer(&a->win, a->width, a->height);
	if (b == NULL) {
		a->need_draw = true; /* both busy; try again on release */
		return;
	}
	tl_render(&a->term, &a->font, &a->pal, b->pixels, b->width, b->height,
		PAD, a->focused);
	tl_window_present(&a->win, b);
	a->need_draw = false;
	a->term.dirty = false;
}

static void send_key(struct app *a, uint32_t code)
{
	struct tl_key_modes m = {
		a->term.app_cursor, a->term.app_keypad, a->kbd.num, a->kbd.caps,
	};
	char seq[TL_KEY_SEQ_MAX];
	size_t n = tl_key_encode(code, tl_kbd_mods(&a->kbd), &m, seq);
	if (n > 0)
		(void)tl_pty_queue(&a->pty, seq, n);
}

static void on_configure(void *ud, int width, int height)
{
	struct app *a = ud;
	if (width <= 0 || height <= 0) {
		/* Ours to choose: keep the current grid. */
		width = a->term.cols * a->font.cell_w + 2 * PAD;
		height = a->term.rows * a->font.cell_h + 2 * PAD;
	}
	a->width = width;
	a->height = height;
	int cols = (width - 2 * PAD) / a->font.cell_w;
	int rows = (height - 2 * PAD) / a->font.cell_h;
	if (cols < 1)
		cols = 1;
	if (rows < 1)
		rows = 1;
	if (cols != a->term.cols || rows != a->term.rows) {
		if (tl_term_resize(&a->term, cols, rows) == 0)
			tl_pty_resize(&a->pty, cols, rows, width, height);
	}
	/* A configure must be answered with a commit at the new size. */
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

static void on_key(void *ud, uint32_t code, bool pressed)
{
	struct app *a = ud;
	tl_kbd_key(&a->kbd, code, pressed);
	if (!pressed) {
		if (code == a->repeat_code)
			a->repeat_code = 0;
		return;
	}
	send_key(a, code);
	if (!tl_key_is_modifier(code) && a->repeat_rate > 0) {
		a->repeat_code = code;
		clock_gettime(CLOCK_MONOTONIC, &a->repeat_at);
		ts_add_ms(&a->repeat_at, a->repeat_delay);
	}
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

static const struct tl_window_events events = {
	on_configure, on_close, on_frame, on_key, on_focus_in, on_focus_out,
	on_modifiers, on_repeat_info,
};

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
	const char *font_path = NULL;
	float pt = 9;
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

	setup_locale();
	static struct app a;
	char err[300];
	tl_palette_default(&a.pal);
	tl_kbd_init(&a.kbd);

	const char *path = tl_font_find(font_path);
	if (path == NULL) {
		fprintf(stderr, "tam-last: no font found; use --font FILE\n");
		return 1;
	}
	if (tl_font_open(&a.font, path, pt, 96, err, sizeof(err)) != 0) {
		fprintf(stderr, "tam-last: %s\n", err);
		return 1;
	}
	if (tl_term_init(&a.term, DEFAULT_COLS, DEFAULT_ROWS) != 0) {
		fprintf(stderr, "tam-last: out of memory\n");
		return 1;
	}
	a.width = DEFAULT_COLS * a.font.cell_w + 2 * PAD;
	a.height = DEFAULT_ROWS * a.font.cell_h + 2 * PAD;

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
	if (tl_pty_spawn(&a.pty, cmd, a.term.cols, a.term.rows, a.width,
		    a.height, err, sizeof(err)) != 0) {
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
		if (a.term.reply_len > 0) {
			(void)tl_pty_queue(&a.pty, a.term.reply, a.term.reply_len);
			a.term.reply_len = 0;
		}
		if (a.term.dirty || a.need_draw)
			draw(&a, false);

		int pty_more = pty_open ? tl_pty_flush(&a.pty) : 0;
		int wl_more = tl_window_flush(&a.win);
		if (wl_more < 0 || pty_more < 0) {
			if (wl_more < 0)
				fprintf(stderr, "tam-last: %s\n", a.win.err);
			break;
		}

		struct pollfd fds[3] = {
			{ tl_window_fd(&a.win), POLLIN | (wl_more ? POLLOUT : 0), 0 },
			{ pty_open ? a.pty.master : -1,
			  POLLIN | (pty_more ? POLLOUT : 0), 0 },
			{ sigchld_pipe[0], POLLIN, 0 },
		};
		int timeout = -1;
		if (a.repeat_code != 0 && a.focused)
			timeout = (int)ms_until(&a.repeat_at);

		if (poll(fds, 3, timeout) < 0 && errno != EINTR) {
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

		if (pty_open && (fds[1].revents & (POLLIN | POLLHUP | POLLERR)))
			pty_open = read_pty(&a);

		if (fds[2].revents & POLLIN) {
			char drain[64];
			while (read(sigchld_pipe[0], drain, sizeof(drain)) > 0)
				;
			if (!child_done)
				child_done = reap(&a, &status);
		}

		if (a.repeat_code != 0 && a.focused &&
		    ms_until(&a.repeat_at) == 0) {
			send_key(&a, a.repeat_code);
			ts_add_ms(&a.repeat_at, 1000 / a.repeat_rate);
		}

		/* Done when the program has exited, or nothing can write to
		 * the terminal any more. */
		if (child_done || !pty_open) {
			if (pty_open)
				pty_open = read_pty(&a); /* its last words */
			if (!child_done)
				child_done = reap(&a, &status);
			if (a.term.dirty)
				draw(&a, true);
			(void)tl_window_flush(&a.win);
			break;
		}
	}

	if (!child_done)
		kill(-a.pty.pid, SIGHUP);
	tl_window_close(&a.win);
	tl_pty_close(&a.pty);
	tl_term_free(&a.term);
	tl_font_close(&a.font);
	return status;
}
