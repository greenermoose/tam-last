/* Safe child-process spawner for desktop notifications (notify-send with a
 * monotonic deadline) and URL launching (xdg-open, never via a shell). */
#include "tam-last/action.h"
#include "tam-last/url.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void add_ms(struct timespec *ts, long ms)
{
	ts->tv_sec += ms / 1000;
	ts->tv_nsec += (ms % 1000) * 1000000L;
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

static long remaining_ms(const struct timespec *deadline)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	long ms = (long)(deadline->tv_sec - now.tv_sec) * 1000 +
		(deadline->tv_nsec - now.tv_nsec) / 1000000L;
	return ms < 0 ? 0 : ms;
}

static void close_inherited_fds(void)
{
	int devnull = open("/dev/null", O_RDWR);
	if (devnull >= 0) {
		(void)dup2(devnull, STDIN_FILENO);
		(void)dup2(devnull, STDOUT_FILENO);
		(void)dup2(devnull, STDERR_FILENO);
		if (devnull > STDERR_FILENO)
			close(devnull);
	}
	long maxfd = sysconf(_SC_OPEN_MAX);
	if (maxfd < 0 || maxfd > 4096)
		maxfd = 1024;
	for (int fd = 3; fd < (int)maxfd; fd++)
		close(fd);
}

void tl_actions_init(struct tl_actions *act)
{
	memset(act, 0, sizeof(*act));
}

void tl_action_reap(struct tl_actions *act)
{
	if (act == NULL)
		return;
	for (int i = 0; i < TL_ACTION_MAX_PROCS; i++) {
		if (act->procs[i].pid <= 0)
			continue;
		int st;
		pid_t r = waitpid(act->procs[i].pid, &st, WNOHANG);
		if (r == act->procs[i].pid || (r < 0 && errno == ECHILD)) {
			act->procs[i].pid = 0;
			act->procs[i].has_deadline = false;
		}
	}
}

int tl_action_notify(struct tl_actions *act, const struct tl_notify *n,
	const char *cmd_override, int timeout_ms)
{
	if (act == NULL || n == NULL)
		return -1;
	if (n->title[0] == '\0' && n->body[0] == '\0')
		return -1;

	tl_action_reap(act);
	int slot = -1;
	for (int i = 0; i < TL_ACTION_MAX_PROCS; i++) {
		if (act->procs[i].pid <= 0) {
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return -1; /* concurrency cap reached: drop burst safely */

	const char *prog = cmd_override != NULL ? cmd_override : "notify-send";
	const char *urg = n->urgency == TL_NOTIFY_LOW ? "low" :
		n->urgency == TL_NOTIFY_CRITICAL ? "critical" : "normal";
	const char *title = n->title[0] != '\0' ? n->title : "tam-last";

	char *argv[9];
	int ac = 0;
	argv[ac++] = (char *)prog;
	argv[ac++] = (char *)"-a";
	argv[ac++] = (char *)"tam-last";
	argv[ac++] = (char *)"-u";
	argv[ac++] = (char *)urg;
	argv[ac++] = (char *)"--";
	argv[ac++] = (char *)title;
	if (n->body[0] != '\0')
		argv[ac++] = (char *)n->body;
	argv[ac] = NULL;

	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		signal(SIGPIPE, SIG_DFL);
		signal(SIGCHLD, SIG_DFL);
		close_inherited_fds();
		execvp(prog, argv);
		_exit(127);
	}

	act->procs[slot].pid = pid;
	act->procs[slot].has_deadline = true;
	clock_gettime(CLOCK_MONOTONIC, &act->procs[slot].deadline);
	add_ms(&act->procs[slot].deadline,
		timeout_ms > 0 ? timeout_ms : TL_NOTIFY_TIMEOUT_MS);
	return 0;
}

int tl_action_open_url(const char *uri)
{
	if (!tl_url_is_safe(uri))
		return -1;
	/* Double-fork so the launched browser/viewer is reparented to init
	 * and cannot linger as a zombie or inherit terminal lifetime. */
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		(void)setsid();
		pid_t grand = fork();
		if (grand == 0) {
			signal(SIGPIPE, SIG_DFL);
			signal(SIGCHLD, SIG_DFL);
			close_inherited_fds();
			char *argv[3] = {
				(char *)"xdg-open", (char *)uri, NULL
			};
			execvp("xdg-open", argv);
			_exit(127);
		}
		_exit(0);
	}
	int st;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
		;
	return 0;
}

int tl_action_next_timeout_ms(const struct tl_actions *act)
{
	if (act == NULL)
		return -1;
	int best = -1;
	for (int i = 0; i < TL_ACTION_MAX_PROCS; i++) {
		if (act->procs[i].pid <= 0 || !act->procs[i].has_deadline)
			continue;
		int ms = (int)remaining_ms(&act->procs[i].deadline);
		if (best < 0 || ms < best)
			best = ms;
	}
	return best;
}

int tl_action_check_deadlines(struct tl_actions *act)
{
	if (act == NULL)
		return 0;
	tl_action_reap(act);
	int killed = 0;
	for (int i = 0; i < TL_ACTION_MAX_PROCS; i++) {
		if (act->procs[i].pid <= 0 || !act->procs[i].has_deadline)
			continue;
		if (remaining_ms(&act->procs[i].deadline) == 0) {
			pid_t pid = act->procs[i].pid;
			(void)kill(pid, SIGKILL);
			int st;
			while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
				;
			act->procs[i].pid = 0;
			act->procs[i].has_deadline = false;
			killed++;
		}
	}
	return killed;
}

void tl_actions_kill_all(struct tl_actions *act)
{
	if (act == NULL)
		return;
	for (int i = 0; i < TL_ACTION_MAX_PROCS; i++) {
		if (act->procs[i].pid <= 0)
			continue;
		pid_t pid = act->procs[i].pid;
		(void)kill(pid, SIGKILL);
		int st;
		while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
			;
		act->procs[i].pid = 0;
		act->procs[i].has_deadline = false;
	}
}
