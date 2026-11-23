/* Bounded external action spawner: notify-send (with monotonic deadline and
 * concurrency cap) and xdg-open (validated URI, never via a shell). */
#ifndef TL_ACTION_H
#define TL_ACTION_H

#include <stdbool.h>
#include <sys/types.h>
#include <time.h>

#include "tam-last/term.h"

#define TL_ACTION_MAX_PROCS 4
#define TL_NOTIFY_TIMEOUT_MS 3000

struct tl_action_proc {
	pid_t pid;
	bool has_deadline;
	struct timespec deadline;
};

struct tl_actions {
	struct tl_action_proc procs[TL_ACTION_MAX_PROCS];
};

void tl_actions_init(struct tl_actions *act);

/* Launch notify-send for n with a timeout_ms deadline (<= 0 uses default).
 * cmd_override is NULL in production (uses "notify-send") or a test binary.
 * Returns 0 if spawned, -1 if dropped (busy cap, empty, or fork error). */
int tl_action_notify(struct tl_actions *act, const struct tl_notify *n,
	const char *cmd_override, int timeout_ms);

/* Validate uri with tl_url_is_safe() and launch xdg-open detached.
 * Returns 0 on success, -1 on unsafe URI or fork failure. */
int tl_action_open_url(const char *uri);

/* Reap any finished tracked children (non-blocking, call on SIGCHLD). */
void tl_action_reap(struct tl_actions *act);

/* Milliseconds until the earliest active deadline, or -1 if none active. */
int tl_action_next_timeout_ms(const struct tl_actions *act);

/* Kill and reap any tracked child whose deadline has expired.
 * Returns the number of expired children terminated. */
int tl_action_check_deadlines(struct tl_actions *act);

/* Terminate and reap all tracked children on shutdown. */
void tl_actions_kill_all(struct tl_actions *act);

#endif
