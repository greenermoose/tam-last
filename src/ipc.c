/* Client/server Unix-domain socket IPC for tam-last --server and --client. */
#include "tam-last/ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static const uint8_t IPC_MAGIC[4] = { 'T', 'L', '0', '1' };

struct enc {
	uint8_t *buf;
	size_t cap;
	size_t pos;
	bool bad;
};

static void enc_bytes(struct enc *e, const void *p, size_t n)
{
	if (e->bad || n > e->cap - e->pos) {
		e->bad = true;
		return;
	}
	if (n > 0) {
		memcpy(e->buf + e->pos, p, n);
		e->pos += n;
	}
}

static void enc_u8(struct enc *e, uint8_t v)
{
	enc_bytes(e, &v, 1);
}

static void enc_u16(struct enc *e, uint16_t v)
{
	enc_bytes(e, &v, sizeof(v));
}

static void enc_u32(struct enc *e, uint32_t v)
{
	enc_bytes(e, &v, sizeof(v));
}

static void enc_i32(struct enc *e, int32_t v)
{
	enc_bytes(e, &v, sizeof(v));
}

static void enc_str(struct enc *e, const char *s)
{
	if (s == NULL) {
		enc_u32(e, 0);
		return;
	}
	size_t n = strlen(s);
	if (n > 65535u) {
		e->bad = true;
		return;
	}
	enc_u32(e, (uint32_t)(n + 1));
	enc_bytes(e, s, n + 1);
}

static size_t enc_finish(struct enc *e)
{
	if (e->bad || e->pos < TL_IPC_HDR_LEN || e->pos > TL_IPC_MAX_MSG)
		return 0;
	uint32_t payload = (uint32_t)(e->pos - TL_IPC_HDR_LEN);
	memcpy(e->buf, IPC_MAGIC, 4);
	memcpy(e->buf + 4, &payload, 4);
	return e->pos;
}

struct dec {
	const uint8_t *buf;
	size_t len;
	size_t pos;
	bool bad;
};

static void dec_bytes(struct dec *d, void *out, size_t n)
{
	if (d->bad || n > d->len - d->pos) {
		d->bad = true;
		return;
	}
	if (n > 0) {
		memcpy(out, d->buf + d->pos, n);
		d->pos += n;
	}
}

static uint8_t dec_u8(struct dec *d)
{
	uint8_t v = 0;
	dec_bytes(d, &v, 1);
	return v;
}

static uint16_t dec_u16(struct dec *d)
{
	uint16_t v = 0;
	dec_bytes(d, &v, sizeof(v));
	return v;
}

static uint32_t dec_u32(struct dec *d)
{
	uint32_t v = 0;
	dec_bytes(d, &v, sizeof(v));
	return v;
}

static int32_t dec_i32(struct dec *d)
{
	int32_t v = 0;
	dec_bytes(d, &v, sizeof(v));
	return v;
}

static char *dec_str_dup(struct dec *d)
{
	uint32_t n = dec_u32(d);
	if (d->bad || n == 0)
		return NULL;
	if (n > d->len - d->pos) {
		d->bad = true;
		return NULL;
	}
	const char *s = (const char *)(d->buf + d->pos);
	if (s[n - 1] != '\0' || memchr(s, '\0', n - 1) != NULL) {
		d->bad = true;
		return NULL;
	}
	d->pos += n;
	char *copy = malloc(n);
	if (copy == NULL) {
		d->bad = true;
		return NULL;
	}
	memcpy(copy, s, n);
	return copy;
}

void tl_ipc_launch_free(struct tl_ipc_launch *req)
{
	if (req == NULL)
		return;
	free(req->cwd);
	free(req->shell);
	free(req->config_path);
	free(req->font_path);
	free(req->layout_name);
	free(req->term_name);
	if (req->argv != NULL) {
		for (size_t i = 0; i < req->argc; i++)
			free(req->argv[i]);
		free(req->argv);
	}
	memset(req, 0, sizeof(*req));
}

size_t tl_ipc_frame_len(const uint8_t hdr[TL_IPC_HDR_LEN])
{
	if (memcmp(hdr, IPC_MAGIC, 4) != 0)
		return 0;
	uint32_t payload = 0;
	memcpy(&payload, hdr + 4, 4);
	if (payload == 0 || payload > TL_IPC_MAX_MSG - TL_IPC_HDR_LEN)
		return 0;
	return TL_IPC_HDR_LEN + (size_t)payload;
}

size_t tl_ipc_encode_launch(const struct tl_ipc_launch *req, uint8_t *buf,
	size_t cap)
{
	if (req == NULL || buf == NULL || cap < TL_IPC_HDR_LEN + 16)
		return 0;
	if (req->argc > TL_IPC_MAX_ARGC)
		return 0;
	struct enc e = { buf, cap, TL_IPC_HDR_LEN, false };
	enc_u8(&e, TL_IPC_LAUNCH);
	enc_u32(&e, req->flags);
	enc_bytes(&e, &req->pt, sizeof(req->pt));
	enc_u8(&e, req->osc52);
	enc_u8(&e, req->notify);
	enc_str(&e, req->cwd);
	enc_str(&e, req->shell);
	enc_str(&e, req->config_path);
	enc_str(&e, req->font_path);
	enc_str(&e, req->layout_name);
	enc_str(&e, req->term_name);
	enc_u16(&e, (uint16_t)req->argc);
	for (size_t i = 0; i < req->argc; i++)
		enc_str(&e, req->argv != NULL ? req->argv[i] : "");
	return enc_finish(&e);
}

int tl_ipc_decode_launch(const uint8_t *buf, size_t len,
	struct tl_ipc_launch *out)
{
	memset(out, 0, sizeof(*out));
	if (buf == NULL || len < TL_IPC_HDR_LEN + 1)
		return -1;
	if (tl_ipc_frame_len(buf) != len)
		return -1;
	struct dec d = { buf, len, TL_IPC_HDR_LEN, false };
	if (dec_u8(&d) != TL_IPC_LAUNCH)
		return -1;
	out->flags = dec_u32(&d);
	dec_bytes(&d, &out->pt, sizeof(out->pt));
	out->osc52 = dec_u8(&d);
	out->notify = dec_u8(&d);
	out->cwd = dec_str_dup(&d);
	out->shell = dec_str_dup(&d);
	out->config_path = dec_str_dup(&d);
	out->font_path = dec_str_dup(&d);
	out->layout_name = dec_str_dup(&d);
	out->term_name = dec_str_dup(&d);
	uint16_t argc = dec_u16(&d);
	if (d.bad || argc > TL_IPC_MAX_ARGC) {
		tl_ipc_launch_free(out);
		return -1;
	}
	if (argc > 0) {
		out->argv = calloc((size_t)argc + 1, sizeof(*out->argv));
		if (out->argv == NULL) {
			tl_ipc_launch_free(out);
			return -1;
		}
		for (uint16_t i = 0; i < argc; i++) {
			out->argv[i] = dec_str_dup(&d);
			out->argc = (size_t)i + 1;
			if (d.bad || out->argv[i] == NULL) {
				tl_ipc_launch_free(out);
				return -1;
			}
		}
		out->argv[argc] = NULL;
	}
	if (d.bad || d.pos != d.len) {
		tl_ipc_launch_free(out);
		return -1;
	}
	return 0;
}

size_t tl_ipc_encode_ack(uint8_t status, const char *msg, uint8_t *buf,
	size_t cap)
{
	if (buf == NULL || cap < TL_IPC_HDR_LEN + 6)
		return 0;
	struct enc e = { buf, cap, TL_IPC_HDR_LEN, false };
	enc_u8(&e, TL_IPC_ACK);
	enc_u8(&e, status);
	enc_str(&e, msg);
	return enc_finish(&e);
}

int tl_ipc_decode_ack(const uint8_t *buf, size_t len, uint8_t *status,
	char *msg, size_t msglen)
{
	if (buf == NULL || len < TL_IPC_HDR_LEN + 2)
		return -1;
	if (tl_ipc_frame_len(buf) != len)
		return -1;
	struct dec d = { buf, len, TL_IPC_HDR_LEN, false };
	if (dec_u8(&d) != TL_IPC_ACK)
		return -1;
	uint8_t st = dec_u8(&d);
	char *s = dec_str_dup(&d);
	if (d.bad || d.pos != d.len) {
		free(s);
		return -1;
	}
	if (status != NULL)
		*status = st;
	if (msg != NULL && msglen > 0) {
		if (s != NULL)
			snprintf(msg, msglen, "%s", s);
		else
			msg[0] = '\0';
	}
	free(s);
	return 0;
}

size_t tl_ipc_encode_exit(int32_t exit_code, uint8_t *buf, size_t cap)
{
	if (buf == NULL || cap < TL_IPC_HDR_LEN + 5)
		return 0;
	struct enc e = { buf, cap, TL_IPC_HDR_LEN, false };
	enc_u8(&e, TL_IPC_EXIT);
	enc_i32(&e, exit_code);
	return enc_finish(&e);
}

int tl_ipc_decode_exit(const uint8_t *buf, size_t len, int32_t *exit_code)
{
	if (buf == NULL || len < TL_IPC_HDR_LEN + 5)
		return -1;
	if (tl_ipc_frame_len(buf) != len)
		return -1;
	struct dec d = { buf, len, TL_IPC_HDR_LEN, false };
	if (dec_u8(&d) != TL_IPC_EXIT)
		return -1;
	int32_t code = dec_i32(&d);
	if (d.bad || d.pos != d.len)
		return -1;
	if (exit_code != NULL)
		*exit_code = code;
	return 0;
}

int tl_ipc_default_socket(char *buf, size_t len, char *err, size_t errlen)
{
	const char *dir = getenv("XDG_RUNTIME_DIR");
	if (dir == NULL || dir[0] == '\0') {
		snprintf(err, errlen, "XDG_RUNTIME_DIR is not set");
		return -1;
	}
	const char *wl = getenv("WAYLAND_DISPLAY");
	if (wl == NULL || wl[0] == '\0')
		wl = "wayland-0";
	/* If WAYLAND_DISPLAY is an absolute path, use its basename. */
	const char *slash = strrchr(wl, '/');
	if (slash != NULL && slash[1] != '\0')
		wl = slash + 1;
	int n = snprintf(buf, len, "%s/tam-last-%s.sock", dir, wl);
	if (n < 0 || (size_t)n >= len) {
		snprintf(err, errlen, "server socket path is too long");
		return -1;
	}
	return 0;
}

static int make_sockaddr(struct sockaddr_un *addr, const char *path,
	char *err, size_t errlen)
{
	memset(addr, 0, sizeof(*addr));
	addr->sun_family = AF_UNIX;
	int n = snprintf(addr->sun_path, sizeof(addr->sun_path), "%s", path);
	if (n < 0 || (size_t)n >= sizeof(addr->sun_path)) {
		snprintf(err, errlen, "socket path is too long: %s", path);
		return -1;
	}
	return 0;
}

int tl_ipc_listen(const char *path, char *err, size_t errlen)
{
	struct sockaddr_un addr;
	if (make_sockaddr(&addr, path, err, errlen) != 0)
		return -1;

	/* Detect an already-running server vs a stale socket file. */
	struct stat st;
	if (lstat(path, &st) == 0) {
		if (!S_ISSOCK(st.st_mode)) {
			snprintf(err, errlen, "%s exists and is not a socket",
				path);
			return -1;
		}
		int probe = socket(AF_UNIX, SOCK_STREAM, 0);
		if (probe >= 0) {
			fcntl(probe, F_SETFL, O_NONBLOCK);
			int cr = connect(probe, (struct sockaddr *)&addr,
				sizeof(addr));
			int ce = errno;
			close(probe);
			if (cr == 0 || ce == EINPROGRESS || ce == EAGAIN) {
				snprintf(err, errlen,
					"server already running on %s", path);
				return -1;
			}
		}
		if (unlink(path) != 0) {
			snprintf(err, errlen, "cannot remove stale socket %s: %s",
				path, strerror(errno));
			return -1;
		}
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		snprintf(err, errlen, "socket: %s", strerror(errno));
		return -1;
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

	mode_t old_mask = umask(0077);
	int br = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	int be = errno;
	umask(old_mask);
	if (br != 0) {
		snprintf(err, errlen, "cannot bind %s: %s", path, strerror(be));
		close(fd);
		return -1;
	}
	if (listen(fd, 16) != 0) {
		snprintf(err, errlen, "cannot listen on %s: %s", path,
			strerror(errno));
		unlink(path);
		close(fd);
		return -1;
	}
	return fd;
}

int tl_ipc_write_all(int fd, const void *buf, size_t len)
{
	const uint8_t *p = buf;
	size_t off = 0;
	while (off < len) {
		ssize_t n = write(fd, p + off, len - off);
		if (n > 0) {
			off += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			struct pollfd pf = { fd, POLLOUT, 0 };
			if (poll(&pf, 1, 500) <= 0)
				return -1;
			continue;
		}
		return -1;
	}
	return 0;
}

static int read_exact(int fd, uint8_t *buf, size_t len)
{
	size_t off = 0;
	while (off < len) {
		ssize_t n = read(fd, buf + off, len - off);
		if (n > 0) {
			off += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		return -1;
	}
	return 0;
}

static int read_frame(int fd, uint8_t *buf, size_t cap, size_t *out_len)
{
	if (read_exact(fd, buf, TL_IPC_HDR_LEN) != 0)
		return -1;
	size_t total = tl_ipc_frame_len(buf);
	if (total == 0 || total > cap)
		return -1;
	if (read_exact(fd, buf + TL_IPC_HDR_LEN, total - TL_IPC_HDR_LEN) != 0)
		return -1;
	*out_len = total;
	return 0;
}

int tl_ipc_run_client(const char *path, const struct tl_ipc_launch *req)
{
	struct sockaddr_un addr;
	char err[256];
	if (make_sockaddr(&addr, path, err, sizeof(err)) != 0) {
		fprintf(stderr, "tam-last: %s\n", err);
		return 1;
	}

	uint8_t *msg = malloc(TL_IPC_MAX_MSG);
	if (msg == NULL) {
		fprintf(stderr, "tam-last: out of memory\n");
		return 1;
	}
	size_t msglen = tl_ipc_encode_launch(req, msg, TL_IPC_MAX_MSG);
	if (msglen == 0) {
		fprintf(stderr, "tam-last: launch request is too large\n");
		free(msg);
		return 1;
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		fprintf(stderr, "tam-last: socket: %s\n", strerror(errno));
		free(msg);
		return 1;
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr, "tam-last: cannot connect to server at %s: %s\n",
			path, strerror(errno));
		close(fd);
		free(msg);
		return 1;
	}
	if (tl_ipc_write_all(fd, msg, msglen) != 0) {
		fprintf(stderr, "tam-last: cannot send request to server: %s\n",
			strerror(errno));
		close(fd);
		free(msg);
		return 1;
	}

	size_t rlen = 0;
	if (read_frame(fd, msg, TL_IPC_MAX_MSG, &rlen) != 0) {
		fprintf(stderr, "tam-last: server closed the connection\n");
		close(fd);
		free(msg);
		return 1;
	}
	uint8_t status = 1;
	char ack_msg[256];
	if (tl_ipc_decode_ack(msg, rlen, &status, ack_msg, sizeof(ack_msg)) != 0) {
		fprintf(stderr, "tam-last: invalid reply from server\n");
		close(fd);
		free(msg);
		return 1;
	}
	if (status != 0) {
		fprintf(stderr, "tam-last: %s\n",
			ack_msg[0] != '\0' ? ack_msg : "server rejected request");
		close(fd);
		free(msg);
		return 1;
	}
	if (req->flags & TL_IPC_F_NO_WAIT) {
		close(fd);
		free(msg);
		return 0;
	}

	/* Wait for the window's program to exit and return its exit code. */
	if (read_frame(fd, msg, TL_IPC_MAX_MSG, &rlen) != 0) {
		close(fd);
		free(msg);
		return 0;
	}
	int32_t exit_code = 0;
	if (tl_ipc_decode_exit(msg, rlen, &exit_code) != 0) {
		close(fd);
		free(msg);
		return 1;
	}
	close(fd);
	free(msg);
	return (int)exit_code;
}
