/* Client/server Unix-domain socket IPC for tam-last --server and --client.
 *
 * Framing: 4-byte magic ("TL01"), uint32_t payload length (native host byte
 * order, capped at TL_IPC_MAX_MSG), uint8_t message type, then payload.
 * Untrusted input is bounds-checked on every read. */
#ifndef TL_IPC_H
#define TL_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TL_IPC_HDR_LEN 8u
#define TL_IPC_MAX_MSG 65536u
#define TL_IPC_MAX_ARGC 256u
#define TL_IPC_HANDSHAKE_TIMEOUT_MS 2000

enum tl_ipc_type {
	TL_IPC_LAUNCH = 1,
	TL_IPC_ACK = 2,
	TL_IPC_EXIT = 3
};

enum tl_ipc_flags {
	TL_IPC_F_PT_SET = 1u << 0,
	TL_IPC_F_OSC52_SET = 1u << 1,
	TL_IPC_F_NOTIFY_SET = 1u << 2,
	TL_IPC_F_KEYS_MODE = 1u << 3,
	TL_IPC_F_NO_WAIT = 1u << 4
};

struct tl_ipc_launch {
	uint32_t flags;
	float pt;
	uint8_t osc52;
	uint8_t notify;
	char *cwd;
	char *shell;
	char *config_path;
	char *font_path;
	char *layout_name;
	char *term_name;
	char **argv;
	size_t argc;
};

void tl_ipc_launch_free(struct tl_ipc_launch *req);

/* Default socket path: $XDG_RUNTIME_DIR/tam-last-<wayland>.sock.
 * Returns 0, or -1 with a message in err. */
int tl_ipc_default_socket(char *buf, size_t len, char *err, size_t errlen);

/* Pure framing & codec helpers (unit-tested without sockets).
 * Return encoded byte length on success, or 0 on overflow/error. */
size_t tl_ipc_encode_launch(const struct tl_ipc_launch *req, uint8_t *buf,
	size_t cap);
/* Decode a complete message (including the 8-byte header). Returns 0 on
 * success (caller must call tl_ipc_launch_free), or -1 on malformed input. */
int tl_ipc_decode_launch(const uint8_t *buf, size_t len,
	struct tl_ipc_launch *out);

size_t tl_ipc_encode_ack(uint8_t status, const char *msg, uint8_t *buf,
	size_t cap);
int tl_ipc_decode_ack(const uint8_t *buf, size_t len, uint8_t *status,
	char *msg, size_t msglen);

size_t tl_ipc_encode_exit(int32_t exit_code, uint8_t *buf, size_t cap);
int tl_ipc_decode_exit(const uint8_t *buf, size_t len, int32_t *exit_code);

/* Validate an 8-byte header and return total message length (header + payload),
 * or 0 if invalid. */
size_t tl_ipc_frame_len(const uint8_t hdr[TL_IPC_HDR_LEN]);

/* Server side: create a listening AF_UNIX socket at path (mode 0600,
 * non-blocking, FD_CLOEXEC). Detects live servers vs stale sockets.
 * Returns fd >= 0, or -1 with a message in err. */
int tl_ipc_listen(const char *path, char *err, size_t errlen);

/* Client side: connect to path, send req, wait for ACK (and EXIT unless
 * TL_IPC_F_NO_WAIT). Prints clear errors to stderr and returns the child's
 * exit code (0 on --no-wait success, 1 on connection/protocol error). */
int tl_ipc_run_client(const char *path, const struct tl_ipc_launch *req);

/* Write all bytes to a (possibly non-blocking) fd with short retry; returns
 * 0 on success, -1 on error. Used for small server replies (ACK/EXIT). */
int tl_ipc_write_all(int fd, const void *buf, size_t len);

#endif
