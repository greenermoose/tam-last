/* The Wayland wire protocol, client side.
 *
 * A message is a header of two 32-bit words in host byte order (the
 * object id, then the size in bytes in the high 16 bits and the opcode in
 * the low 16), then its arguments, each padded to 32 bits. Descriptors
 * travel beside the bytes as SCM_RIGHTS control messages and are taken in
 * order by the messages that carry an fd argument. Written fresh from the
 * protocol documentation; libwayland's src/connection.c was read to
 * check the details. */
#define _DEFAULT_SOURCE /* CMSG_SPACE and CMSG_LEN */
#include "tam-last/wire.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void tl_wire_init(struct tl_wire *w, int fd)
{
	memset(w, 0, sizeof(*w));
	w->fd = fd;
	/* Id 1 is always wl_display. */
	w->objects[1].iface = TL_IF_DISPLAY;
	w->next_id = 2;
}

int tl_wire_connect(struct tl_wire *w, char *err, size_t errlen)
{
	const char *name = getenv("WAYLAND_DISPLAY");
	const char *dir = getenv("XDG_RUNTIME_DIR");
	struct sockaddr_un addr;
	int n;

	if (name == NULL || name[0] == '\0')
		name = "wayland-0";
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	if (name[0] == '/') {
		n = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", name);
	} else {
		if (dir == NULL || dir[0] == '\0') {
			snprintf(err, errlen, "XDG_RUNTIME_DIR is not set");
			return -1;
		}
		n = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s",
			dir, name);
	}
	if (n < 0 || (size_t)n >= sizeof(addr.sun_path)) {
		snprintf(err, errlen, "Wayland socket path is too long");
		return -1;
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		snprintf(err, errlen, "socket: %s", strerror(errno));
		return -1;
	}
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ||
	    connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		snprintf(err, errlen, "cannot connect to %s: %s",
			addr.sun_path, strerror(errno));
		close(fd);
		return -1;
	}
	if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0) {
		snprintf(err, errlen, "fcntl: %s", strerror(errno));
		close(fd);
		return -1;
	}
	tl_wire_init(w, fd);
	return 0;
}

void tl_wire_close(struct tl_wire *w)
{
	for (size_t i = 0; i < w->in_nfds; i++)
		close(w->in_fds[i]);
	for (size_t i = 0; i < w->out_nfds; i++)
		close(w->out_fds[i]);
	w->in_nfds = w->out_nfds = 0;
	if (w->fd >= 0)
		close(w->fd);
	w->fd = -1;
}

uint32_t tl_wire_new_id(struct tl_wire *w, enum tl_iface iface, void *data)
{
	/* Lowest free id first, as libwayland does, so the server's own
	 * table stays small. */
	for (uint32_t id = 2; id < TL_WIRE_OBJECTS; id++) {
		if (w->objects[id].iface == TL_IF_NONE) {
			w->objects[id].iface = iface;
			w->objects[id].data = data;
			return id;
		}
	}
	return 0;
}

void tl_wire_server_id(struct tl_wire *w, uint32_t id, enum tl_iface iface,
	void *data)
{
	if (id < TL_WIRE_SERVER_ID_START)
		return;
	for (size_t i = 0; i < TL_WIRE_SERVER_OBJECTS; i++) {
		if (w->server_objects[i].id == 0 || w->server_objects[i].id == id) {
			w->server_objects[i].id = id;
			w->server_objects[i].obj.iface = iface;
			w->server_objects[i].obj.data = data;
			return;
		}
	}
}

void tl_wire_free_id(struct tl_wire *w, uint32_t id)
{
	if (id >= 2 && id < TL_WIRE_OBJECTS) {
		w->objects[id].iface = TL_IF_NONE;
		w->objects[id].data = NULL;
	} else if (id >= TL_WIRE_SERVER_ID_START) {
		for (size_t i = 0; i < TL_WIRE_SERVER_OBJECTS; i++) {
			if (w->server_objects[i].id == id) {
				w->server_objects[i].id = 0;
				w->server_objects[i].obj.iface = TL_IF_NONE;
				w->server_objects[i].obj.data = NULL;
				return;
			}
		}
	}
}

void tl_wire_zombie(struct tl_wire *w, uint32_t id)
{
	if (id >= 2 && id < TL_WIRE_OBJECTS) {
		w->objects[id].iface = TL_IF_ZOMBIE;
		w->objects[id].data = NULL;
	} else if (id >= TL_WIRE_SERVER_ID_START) {
		for (size_t i = 0; i < TL_WIRE_SERVER_OBJECTS; i++) {
			if (w->server_objects[i].id == id) {
				w->server_objects[i].id = 0;
				w->server_objects[i].obj.iface = TL_IF_NONE;
				w->server_objects[i].obj.data = NULL;
				return;
			}
		}
	}
}

const struct tl_object *tl_wire_object(const struct tl_wire *w, uint32_t id)
{
	if (id == 0)
		return NULL;
	if (id < TL_WIRE_OBJECTS)
		return &w->objects[id];
	if (id >= TL_WIRE_SERVER_ID_START) {
		for (size_t i = 0; i < TL_WIRE_SERVER_OBJECTS; i++) {
			if (w->server_objects[i].id == id)
				return &w->server_objects[i].obj;
		}
	}
	return NULL;
}

void tl_msg_init(struct tl_msg *m)
{
	m->nwords = 0;
	m->nfds = 0;
	m->overflow = false;
}

void tl_msg_u32(struct tl_msg *m, uint32_t v)
{
	/* Two words are kept for the header. */
	if (m->nwords + 1 > TL_WIRE_MSG_MAX / 4 - 2) {
		m->overflow = true;
		return;
	}
	m->words[m->nwords++] = v;
}

void tl_msg_i32(struct tl_msg *m, int32_t v)
{
	tl_msg_u32(m, (uint32_t)v);
}

void tl_msg_str(struct tl_msg *m, const char *s)
{
	if (s == NULL) {
		tl_msg_u32(m, 0);
		return;
	}
	size_t len = strlen(s) + 1; /* the length counts the NUL */
	size_t words = (len + 3) / 4;
	if (len > TL_WIRE_MSG_MAX ||
	    m->nwords + 1 + words > TL_WIRE_MSG_MAX / 4 - 2) {
		m->overflow = true;
		return;
	}
	m->words[m->nwords++] = (uint32_t)len;
	m->words[m->nwords + words - 1] = 0; /* zero the padding */
	memcpy(&m->words[m->nwords], s, len);
	m->nwords += words;
}

void tl_msg_fd(struct tl_msg *m, int fd)
{
	if (m->nfds >= sizeof(m->fds) / sizeof(m->fds[0])) {
		m->overflow = true;
		return;
	}
	m->fds[m->nfds++] = fd;
}

int tl_wire_send(struct tl_wire *w, uint32_t object, uint16_t opcode,
	const struct tl_msg *m)
{
	size_t size = (m->nwords + 2) * 4;

	if (w->failed || m->overflow)
		return -1;
	if (w->out_len + size > sizeof(w->out) ||
	    w->out_nfds + m->nfds > TL_WIRE_FDS) {
		if (tl_wire_flush(w) != 0 || w->out_len + size > sizeof(w->out))
			return -1;
	}
	uint32_t header[2] = { object, (uint32_t)(size << 16) | opcode };
	memcpy(w->out + w->out_len, header, sizeof(header));
	memcpy(w->out + w->out_len + 8, m->words, m->nwords * 4);
	for (size_t i = 0; i < m->nfds; i++) {
		int dup = fcntl(m->fds[i], F_DUPFD_CLOEXEC, 0);
		if (dup < 0) {
			w->failed = true;
			return -1;
		}
		w->out_fds[w->out_nfds++] = dup;
	}
	w->out_len += size;
	return 0;
}

bool tl_wire_pending(const struct tl_wire *w)
{
	return w->out_len > 0;
}

int tl_wire_flush(struct tl_wire *w)
{
	while (w->out_len > 0) {
		struct iovec iov = { w->out, w->out_len };
		union {
			char buf[CMSG_SPACE(sizeof(int) * TL_WIRE_FDS)];
			struct cmsghdr align;
		} ctl;
		struct msghdr msg;
		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		if (w->out_nfds > 0) {
			size_t n = sizeof(int) * w->out_nfds;
			memset(&ctl, 0, sizeof(ctl));
			msg.msg_control = ctl.buf;
			msg.msg_controllen = CMSG_SPACE(n);
			struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
			c->cmsg_level = SOL_SOCKET;
			c->cmsg_type = SCM_RIGHTS;
			c->cmsg_len = CMSG_LEN(n);
			memcpy(CMSG_DATA(c), w->out_fds, n);
		}
		ssize_t sent = sendmsg(w->fd, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
		if (sent < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 1;
			w->failed = true;
			return -1;
		}
		/* The descriptors went with the first byte. */
		for (size_t i = 0; i < w->out_nfds; i++)
			close(w->out_fds[i]);
		w->out_nfds = 0;
		memmove(w->out, w->out + sent, w->out_len - (size_t)sent);
		w->out_len -= (size_t)sent;
	}
	return 0;
}

long tl_wire_read(struct tl_wire *w)
{
	if (w->in_off > 0) {
		memmove(w->in, w->in + w->in_off, w->in_len - w->in_off);
		w->in_len -= w->in_off;
		w->in_off = 0;
	}
	if (w->in_len >= sizeof(w->in)) {
		errno = ENOBUFS;
		return -1;
	}

	struct iovec iov = { w->in + w->in_len, sizeof(w->in) - w->in_len };
	union {
		char buf[CMSG_SPACE(sizeof(int) * TL_WIRE_FDS)];
		struct cmsghdr align;
	} ctl;
	struct msghdr msg;
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = ctl.buf;
	msg.msg_controllen = sizeof(ctl.buf);

	ssize_t got;
	do {
		got = recvmsg(w->fd, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
	} while (got < 0 && errno == EINTR);
	if (got < 0)
		return -1;

	for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c != NULL;
	     c = CMSG_NXTHDR(&msg, c)) {
		if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS)
			continue;
		size_t n = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
		for (size_t i = 0; i < n; i++) {
			int fd;
			memcpy(&fd, CMSG_DATA(c) + i * sizeof(int), sizeof(int));
			if (w->in_nfds < TL_WIRE_FDS) {
				w->in_fds[w->in_nfds++] = fd;
			} else {
				close(fd);
				w->failed = true;
			}
		}
	}
	if (msg.msg_flags & MSG_CTRUNC)
		w->failed = true;
	w->in_len += (size_t)got;
	return (long)got;
}

int tl_wire_next(struct tl_wire *w, struct tl_event *ev)
{
	size_t avail = w->in_len - w->in_off;
	uint32_t header[2];

	if (avail < 8)
		return 0;
	memcpy(header, w->in + w->in_off, 8);
	size_t size = header[1] >> 16;
	if (size < 8 || size % 4 != 0)
		return -1;
	if (avail < size)
		return 0;
	ev->object = header[0];
	ev->opcode = (uint16_t)(header[1] & 0xffff);
	ev->args = w->in + w->in_off + 8;
	ev->len = size - 8;
	ev->pos = 0;
	ev->wire = w;
	ev->bad = false;
	w->in_off += size;
	return 1;
}

uint32_t tl_ev_u32(struct tl_event *ev)
{
	uint32_t v;
	if (ev->len - ev->pos < 4) {
		ev->bad = true;
		return 0;
	}
	memcpy(&v, ev->args + ev->pos, 4);
	ev->pos += 4;
	return v;
}

int32_t tl_ev_i32(struct tl_event *ev)
{
	return (int32_t)tl_ev_u32(ev);
}

const void *tl_ev_array(struct tl_event *ev, size_t *len)
{
	uint32_t n = tl_ev_u32(ev);
	size_t padded = ((size_t)n + 3) & ~(size_t)3;
	*len = 0;
	if (ev->bad || padded > ev->len - ev->pos) {
		ev->bad = true;
		return NULL;
	}
	const void *p = ev->args + ev->pos;
	ev->pos += padded;
	*len = n;
	return p;
}

const char *tl_ev_str(struct tl_event *ev)
{
	size_t len;
	const char *s = tl_ev_array(ev, &len);
	if (ev->bad || len == 0)
		return NULL;
	if (s[len - 1] != '\0' || memchr(s, '\0', len) != s + len - 1) {
		ev->bad = true;
		return NULL;
	}
	return s;
}

int tl_ev_fd(struct tl_event *ev)
{
	struct tl_wire *w = ev->wire;
	if (w->in_nfds == 0) {
		ev->bad = true;
		return -1;
	}
	int fd = w->in_fds[0];
	memmove(w->in_fds, w->in_fds + 1, (w->in_nfds - 1) * sizeof(int));
	w->in_nfds--;
	return fd;
}
