/* The pseudo-terminal: posix_openpt for the master side, and a child that
 * starts a new session, makes the slave side its controlling terminal,
 * and runs the program. Between the two sides sits the kernel's line
 * discipline (arch/key-path.md, "the tty layer"). */
#include "tam-last/pty.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define PTY_QUEUE_MAX (1u << 20)

static void set_size(int fd, int cols, int rows, int width, int height)
{
	struct winsize ws = {
		.ws_row = (unsigned short)rows,
		.ws_col = (unsigned short)cols,
		.ws_xpixel = (unsigned short)width,
		.ws_ypixel = (unsigned short)height,
	};
	(void)ioctl(fd, TIOCSWINSZ, &ws);
}

/* In the child, between fork and exec. tam-last has one thread, so the
 * child may call ordinary library functions such as execvp. */
static void child(const char *slave, char *const argv[], const char *shell)
{
	sigset_t none;
	sigemptyset(&none);
	sigprocmask(SIG_SETMASK, &none, NULL);
	signal(SIGCHLD, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);

	if (setsid() < 0)
		_exit(126);
	int fd = open(slave, O_RDWR);
	if (fd < 0)
		_exit(126);
	if (ioctl(fd, TIOCSCTTY, 0) < 0)
		_exit(126);
	if (dup2(fd, 0) < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0)
		_exit(126);
	if (fd > 2)
		close(fd);
	if (argv != NULL)
		execvp(argv[0], argv);
	else
		execl(shell, shell, (char *)NULL);
	/* stderr is the new terminal, so the message shows in the window. */
	static const char msg[] = "tam-last: cannot run the program\r\n";
	(void)!write(2, msg, sizeof(msg) - 1);
	_exit(127);
}

int tl_pty_spawn(struct tl_pty *p, char *const argv[], int cols, int rows,
	int width, int height, char *err, size_t errlen)
{
	memset(p, 0, sizeof(*p));
	p->master = -1;

	int m = posix_openpt(O_RDWR | O_NOCTTY);
	if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) {
		snprintf(err, errlen, "cannot open a pseudo-terminal: %s",
			strerror(errno));
		if (m >= 0)
			close(m);
		return -1;
	}
	const char *name = ptsname(m);
	char slave[128];
	if (name == NULL || snprintf(slave, sizeof(slave), "%s", name) >=
	    (int)sizeof(slave)) {
		snprintf(err, errlen, "cannot name the pseudo-terminal");
		close(m);
		return -1;
	}
	set_size(m, cols, rows, width, height);
	fcntl(m, F_SETFD, FD_CLOEXEC);

	const char *shell = getenv("SHELL");
	if (shell == NULL || shell[0] != '/')
		shell = "/bin/sh";

	/* Set before fork, so the child calls nothing unsafe. */
	setenv("TERM", "xterm-256color", 1);
	setenv("COLORTERM", "truecolor", 1);
	setenv("TERM_PROGRAM", "tam-last", 1);
	unsetenv("COLUMNS");
	unsetenv("LINES");

	pid_t pid = fork();
	if (pid < 0) {
		snprintf(err, errlen, "fork: %s", strerror(errno));
		close(m);
		return -1;
	}
	if (pid == 0)
		child(slave, argv, shell);

	fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK);
	p->master = m;
	p->pid = pid;
	return 0;
}

void tl_pty_resize(struct tl_pty *p, int cols, int rows, int width,
	int height)
{
	/* The kernel sends SIGWINCH to the foreground process group. */
	if (p->master >= 0)
		set_size(p->master, cols, rows, width, height);
}

int tl_pty_queue(struct tl_pty *p, const void *data, size_t len)
{
	if (len == 0)
		return 0;
	if (p->out_len + len > PTY_QUEUE_MAX)
		return -1;
	if (p->out_len + len > p->out_cap) {
		size_t cap = p->out_cap ? p->out_cap : 4096;
		while (cap < p->out_len + len)
			cap *= 2;
		char *n = realloc(p->out, cap);
		if (n == NULL)
			return -1;
		p->out = n;
		p->out_cap = cap;
	}
	memcpy(p->out + p->out_len, data, len);
	p->out_len += len;
	return 0;
}

int tl_pty_flush(struct tl_pty *p)
{
	size_t done = 0;
	if (p->out_len == 0)
		return 0;
	while (done < p->out_len) {
		ssize_t n = write(p->master, p->out + done, p->out_len - done);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			return -1;
		}
		done += (size_t)n;
	}
	memmove(p->out, p->out + done, p->out_len - done);
	p->out_len -= done;
	return p->out_len > 0 ? 1 : 0;
}

void tl_pty_close(struct tl_pty *p)
{
	if (p->master >= 0)
		close(p->master);
	p->master = -1;
	free(p->out);
	p->out = NULL;
	p->out_len = p->out_cap = 0;
}
