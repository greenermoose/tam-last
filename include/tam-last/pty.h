/* The pseudo-terminal pair and the program running on it. */
#ifndef TL_PTY_H
#define TL_PTY_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct tl_pty {
	int master;
	pid_t pid;
	/* Bytes waiting to be written to the program (keys, replies). */
	char *out;
	size_t out_len, out_cap;
};

/* Start argv (NULL: shell_override, else $SHELL, else /bin/sh) on a new pty
 * of the given size, optionally chdir'ing into cwd in the child.
 * term_name overrides $TERM when non-NULL and non-empty; otherwise tam-last
 * is used when its terminfo entry is installed, falling back to xterm-256color.
 * Never mutates the parent process's environment or cwd.
 * Returns 0, or -1 with a message in err. */
int tl_pty_spawn_in(struct tl_pty *p, char *const argv[], const char *term_name,
	const char *cwd, const char *shell_override, int cols, int rows,
	int width, int height, char *err, size_t errlen);
int tl_pty_spawn(struct tl_pty *p, char *const argv[], const char *term_name,
	int cols, int rows, int width, int height, char *err, size_t errlen);
void tl_pty_resize(struct tl_pty *p, int cols, int rows, int width,
	int height);
/* Queue bytes for the program; returns -1 when the queue is full (1 MiB). */
int tl_pty_queue(struct tl_pty *p, const void *data, size_t len);
/* Write what is queued without blocking: 0 done, 1 more left, -1 error. */
int tl_pty_flush(struct tl_pty *p);
void tl_pty_close(struct tl_pty *p);

#endif
