/* The Wayland wire protocol, client side: one socket, message framing,
 * object ids and file-descriptor passing. No libwayland. */
#ifndef TL_WIRE_H
#define TL_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* libwayland's own limit on one message, header included. */
#define TL_WIRE_MSG_MAX 4096
#define TL_WIRE_BUF 16384
#define TL_WIRE_FDS 28
/* Object ids are reused after delete_id, so they stay small and an array
 * indexed by id is enough. */
#define TL_WIRE_OBJECTS 1024

/* The interfaces tam-last speaks. TL_IF_NONE marks a free id. */
enum tl_iface {
	TL_IF_NONE = 0,
	TL_IF_DISPLAY,
	TL_IF_REGISTRY,
	TL_IF_CALLBACK,
	TL_IF_COMPOSITOR,
	TL_IF_SURFACE,
	TL_IF_SHM,
	TL_IF_SHM_POOL,
	TL_IF_BUFFER,
	TL_IF_SEAT,
	TL_IF_KEYBOARD,
	TL_IF_WM_BASE,
	TL_IF_XDG_SURFACE,
	TL_IF_XDG_TOPLEVEL,
	TL_IF_VIEWPORTER,
	TL_IF_VIEWPORT,
	TL_IF_FRACTIONAL_MANAGER,
	TL_IF_FRACTIONAL_SCALE,
	/* Destroyed by us, waiting for the server's delete_id. */
	TL_IF_ZOMBIE
};

struct tl_object {
	enum tl_iface iface;
	void *data;
};

struct tl_wire {
	int fd;
	unsigned char in[TL_WIRE_BUF];
	size_t in_off; /* start of the first message not yet taken */
	size_t in_len;
	int in_fds[TL_WIRE_FDS];
	size_t in_nfds;
	unsigned char out[TL_WIRE_BUF];
	size_t out_len;
	int out_fds[TL_WIRE_FDS];
	size_t out_nfds;
	struct tl_object objects[TL_WIRE_OBJECTS];
	uint32_t next_id;
	bool failed;
};

/* One message being built. Arguments are 32-bit words. */
struct tl_msg {
	uint32_t words[TL_WIRE_MSG_MAX / 4];
	size_t nwords;
	int fds[4];
	size_t nfds;
	bool overflow;
};

/* One received message being decoded. Any read past the end, or any
 * malformed string, sets bad; the caller checks it once at the end. */
struct tl_event {
	uint32_t object;
	uint16_t opcode;
	const unsigned char *args;
	size_t len;
	size_t pos;
	struct tl_wire *wire;
	bool bad;
};

/* Initialise on an already connected socket (used by the tests). */
void tl_wire_init(struct tl_wire *w, int fd);
/* Connect to $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY (default wayland-0), or an
 * absolute $WAYLAND_DISPLAY. Returns 0, or -1 with a message in err. */
int tl_wire_connect(struct tl_wire *w, char *err, size_t errlen);
void tl_wire_close(struct tl_wire *w);

/* Allocate a client object id for iface. Returns 0 when none are left. */
uint32_t tl_wire_new_id(struct tl_wire *w, enum tl_iface iface, void *data);
/* The server confirmed the id is gone (wl_display.delete_id). */
void tl_wire_free_id(struct tl_wire *w, uint32_t id);
/* We destroyed the object; ignore its events until delete_id. */
void tl_wire_zombie(struct tl_wire *w, uint32_t id);
const struct tl_object *tl_wire_object(const struct tl_wire *w, uint32_t id);

void tl_msg_init(struct tl_msg *m);
void tl_msg_u32(struct tl_msg *m, uint32_t v);
void tl_msg_i32(struct tl_msg *m, int32_t v);
void tl_msg_str(struct tl_msg *m, const char *s);
void tl_msg_fd(struct tl_msg *m, int fd);
/* Queue a request. The fds in m are duplicated, so the caller keeps its
 * own. Returns 0, or -1 when the queue is full even after a flush. */
int tl_wire_send(struct tl_wire *w, uint32_t object, uint16_t opcode,
	const struct tl_msg *m);
/* Write queued requests. Returns 0 when all are written, 1 when the
 * socket is full (poll for POLLOUT), -1 on error. */
int tl_wire_flush(struct tl_wire *w);
bool tl_wire_pending(const struct tl_wire *w);

/* Read what the socket has. Returns bytes read (0 at EOF), or -1 on error
 * (errno EAGAIN when nothing was ready). */
long tl_wire_read(struct tl_wire *w);
/* Take the next complete message. Returns 1 with ev filled, 0 when no
 * complete message is buffered, -1 on a malformed header. ev points into
 * the buffer and is valid until the next call. */
int tl_wire_next(struct tl_wire *w, struct tl_event *ev);

uint32_t tl_ev_u32(struct tl_event *ev);
int32_t tl_ev_i32(struct tl_event *ev);
/* NULL for a null string. A string without its NUL marks ev bad. */
const char *tl_ev_str(struct tl_event *ev);
const void *tl_ev_array(struct tl_event *ev, size_t *len);
/* The next received descriptor, or -1 (and bad) when none arrived. The
 * caller owns it. */
int tl_ev_fd(struct tl_event *ev);

#endif
