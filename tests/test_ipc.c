/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tests for client/server IPC framing, socket lifecycle, and shared font cache. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "check.h"
#include "tam-last/font.h"
#include "tam-last/ipc.h"

static void test_default_socket(void)
{
	char buf[256], err[128];

	setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
	setenv("WAYLAND_DISPLAY", "wayland-1", 1);
	CHECK(tl_ipc_default_socket(buf, sizeof(buf), err, sizeof(err)) == 0);
	CHECK(strcmp(buf, "/run/user/1000/tam-last-wayland-1.sock") == 0);

	setenv("WAYLAND_DISPLAY", "/tmp/custom/wayland-7", 1);
	CHECK(tl_ipc_default_socket(buf, sizeof(buf), err, sizeof(err)) == 0);
	CHECK(strcmp(buf, "/run/user/1000/tam-last-wayland-7.sock") == 0);

	unsetenv("WAYLAND_DISPLAY");
	CHECK(tl_ipc_default_socket(buf, sizeof(buf), err, sizeof(err)) == 0);
	CHECK(strcmp(buf, "/run/user/1000/tam-last-wayland-0.sock") == 0);

	unsetenv("XDG_RUNTIME_DIR");
	CHECK(tl_ipc_default_socket(buf, sizeof(buf), err, sizeof(err)) == -1);
}

static void test_codec_roundtrip(void)
{
	char *args[] = { "sh", "-c", "echo hi", NULL };
	struct tl_ipc_launch req;
	memset(&req, 0, sizeof(req));
	req.flags = TL_IPC_F_PT_SET | TL_IPC_F_OSC52_SET | TL_IPC_F_NOTIFY_SET |
		TL_IPC_F_KEYS_MODE | TL_IPC_F_NO_WAIT;
	req.pt = 11.5f;
	req.osc52 = 2;
	req.notify = 1;
	req.cwd = "/tmp/work";
	req.shell = "/bin/zsh";
	req.config_path = "/tmp/c.ini";
	req.font_path = "/tmp/f.ttf";
	req.layout_name = "us-intl";
	req.term_name = "tam-last";
	req.argv = args;
	req.argc = 3;

	uint8_t buf[4096];
	size_t n = tl_ipc_encode_launch(&req, buf, sizeof(buf));
	CHECK(n > TL_IPC_HDR_LEN);
	CHECK(tl_ipc_frame_len(buf) == n);

	struct tl_ipc_launch dec;
	CHECK(tl_ipc_decode_launch(buf, n, &dec) == 0);
	CHECK(dec.flags == req.flags);
	CHECK(dec.pt == req.pt);
	CHECK(dec.osc52 == 2 && dec.notify == 1);
	CHECK(dec.cwd != NULL && strcmp(dec.cwd, "/tmp/work") == 0);
	CHECK(dec.shell != NULL && strcmp(dec.shell, "/bin/zsh") == 0);
	CHECK(dec.config_path != NULL && strcmp(dec.config_path, "/tmp/c.ini") == 0);
	CHECK(dec.font_path != NULL && strcmp(dec.font_path, "/tmp/f.ttf") == 0);
	CHECK(dec.layout_name != NULL && strcmp(dec.layout_name, "us-intl") == 0);
	CHECK(dec.term_name != NULL && strcmp(dec.term_name, "tam-last") == 0);
	CHECK(dec.argc == 3 && dec.argv != NULL);
	CHECK(strcmp(dec.argv[0], "sh") == 0);
	CHECK(strcmp(dec.argv[1], "-c") == 0);
	CHECK(strcmp(dec.argv[2], "echo hi") == 0);
	CHECK(dec.argv[3] == NULL);
	tl_ipc_launch_free(&dec);

	/* ACK and EXIT roundtrips. */
	n = tl_ipc_encode_ack(1, "bad font", buf, sizeof(buf));
	CHECK(n > TL_IPC_HDR_LEN);
	uint8_t st = 0;
	char msg[64];
	CHECK(tl_ipc_decode_ack(buf, n, &st, msg, sizeof(msg)) == 0);
	CHECK(st == 1 && strcmp(msg, "bad font") == 0);

	n = tl_ipc_encode_exit(42, buf, sizeof(buf));
	CHECK(n == TL_IPC_HDR_LEN + 5);
	int32_t code = -1;
	CHECK(tl_ipc_decode_exit(buf, n, &code) == 0 && code == 42);
}

static void test_malformed_inputs(void)
{
	uint8_t buf[64];
	memset(buf, 0, sizeof(buf));
	CHECK(tl_ipc_frame_len(buf) == 0); /* bad magic */

	memcpy(buf, "TL01", 4);
	uint32_t zero = 0;
	memcpy(buf + 4, &zero, 4);
	CHECK(tl_ipc_frame_len(buf) == 0); /* zero payload */

	uint32_t huge = TL_IPC_MAX_MSG + 100;
	memcpy(buf + 4, &huge, 4);
	CHECK(tl_ipc_frame_len(buf) == 0); /* oversized payload */

	struct tl_ipc_launch dec;
	CHECK(tl_ipc_decode_launch(buf, sizeof(buf), &dec) == -1);

	/* Truncated launch packet. */
	struct tl_ipc_launch req;
	memset(&req, 0, sizeof(req));
	req.cwd = "/tmp";
	size_t n = tl_ipc_encode_launch(&req, buf, sizeof(buf));
	CHECK(n > TL_IPC_HDR_LEN);
	CHECK(tl_ipc_decode_launch(buf, n - 1, &dec) == -1);

	/* Embedded NUL inside string must be rejected. */
	uint8_t bad[64];
	memcpy(bad, buf, n);
	/* Corrupt '/tmp\0' -> '/t\0p\0' */
	for (size_t i = TL_IPC_HDR_LEN; i + 4 < n; i++) {
		if (memcmp(bad + i, "/tmp", 4) == 0) {
			bad[i + 2] = '\0';
			break;
		}
	}
	CHECK(tl_ipc_decode_launch(bad, n, &dec) == -1);
}

static void test_socket_lifecycle_and_handshake(void)
{
	char tmpl[] = "/tmp/tam-last-ipc-test.XXXXXX";
	char *dir = mkdtemp(tmpl);
	CHECK(dir != NULL);
	char sock[256], err[128];
	snprintf(sock, sizeof(sock), "%s/srv.sock", dir);

	int lfd = tl_ipc_listen(sock, err, sizeof(err));
	CHECK(lfd >= 0);

	/* Socket permissions must be owner-only (0600 or 0700). */
	struct stat st;
	CHECK(stat(sock, &st) == 0 && (st.st_mode & 0077) == 0);

	/* A second server on the same live socket must fail cleanly. */
	CHECK(tl_ipc_listen(sock, err, sizeof(err)) == -1);
	CHECK(strstr(err, "already running") != NULL);
	/* Drain the probe connection queued by the duplicate-server check. */
	int pfd = accept(lfd, NULL, NULL);
	if (pfd >= 0)
		close(pfd);

	/* Fork a client that connects, sends launch, receives ACK + EXIT(7). */
	pid_t pid = fork();
	CHECK(pid >= 0);
	if (pid == 0) {
		struct tl_ipc_launch req;
		memset(&req, 0, sizeof(req));
		req.cwd = dir;
		int rc = tl_ipc_run_client(sock, &req);
		_exit(rc);
	}

	struct pollfd pf = { lfd, POLLIN, 0 };
	CHECK(poll(&pf, 1, 2000) == 1);
	int cfd = accept(lfd, NULL, NULL);
	CHECK(cfd >= 0);

	uint8_t rbuf[4096];
	size_t rlen = 0, want = 0;
	for (;;) {
		ssize_t n = read(cfd, rbuf + rlen, sizeof(rbuf) - rlen);
		if (n <= 0)
			break;
		rlen += (size_t)n;
		if (want == 0 && rlen >= TL_IPC_HDR_LEN)
			want = tl_ipc_frame_len(rbuf);
		if (want > 0 && rlen >= want)
			break;
	}
	CHECK(want > 0 && rlen == want);
	struct tl_ipc_launch got;
	CHECK(tl_ipc_decode_launch(rbuf, rlen, &got) == 0);
	CHECK(got.cwd != NULL && strcmp(got.cwd, dir) == 0);
	tl_ipc_launch_free(&got);

	uint8_t ack[64];
	size_t an = tl_ipc_encode_ack(0, NULL, ack, sizeof(ack));
	CHECK(tl_ipc_write_all(cfd, ack, an) == 0);
	size_t en = tl_ipc_encode_exit(7, ack, sizeof(ack));
	CHECK(tl_ipc_write_all(cfd, ack, en) == 0);
	close(cfd);

	int wst = 0;
	CHECK(waitpid(pid, &wst, 0) == pid);
	CHECK(WIFEXITED(wst) && WEXITSTATUS(wst) == 7);

	/* Closing lfd leaves a stale socket; a new tl_ipc_listen replaces it. */
	close(lfd);
	int lfd2 = tl_ipc_listen(sock, err, sizeof(err));
	CHECK(lfd2 >= 0);
	close(lfd2);
	unlink(sock);
	rmdir(dir);
}

static void test_font_cache(void)
{
	const char *font = tl_font_find(NULL);
	if (font == NULL)
		return; /* no system font installed in this environment */

	struct tl_font_cache cache;
	tl_font_cache_init(&cache);

	struct tl_font_key k1;
	memset(&k1, 0, sizeof(k1));
	snprintf(k1.path, sizeof(k1.path), "%s", font);
	k1.pt_tenths = 90;
	k1.dpi = 96;

	char err[128];
	struct tl_font *f1 = tl_font_cache_acquire(&cache, &k1, err, sizeof(err));
	CHECK(f1 != NULL);
	struct tl_font *f2 = tl_font_cache_acquire(&cache, &k1, err, sizeof(err));
	CHECK(f2 == f1); /* exact shared instance */
	CHECK(cache.len == 1 && cache.entries[0]->refcount == 2);

	/* Different DPI creates a distinct entry. */
	struct tl_font_key k2 = k1;
	k2.dpi = 144;
	struct tl_font *f3 = tl_font_cache_acquire(&cache, &k2, err, sizeof(err));
	CHECK(f3 != NULL && f3 != f1);
	CHECK(cache.len == 2);

	/* Releasing keeps entries warm up to TL_FONT_CACHE_WARM. */
	tl_font_cache_release(&cache, f1);
	tl_font_cache_release(&cache, f2);
	tl_font_cache_release(&cache, f3);
	CHECK(cache.len == 2);

	/* Re-acquiring k1 hits the warm cache and returns the same pointer. */
	struct tl_font *f4 = tl_font_cache_acquire(&cache, &k1, err, sizeof(err));
	CHECK(f4 == f1);
	tl_font_cache_release(&cache, f4);

	/* Filling beyond TL_FONT_CACHE_WARM evicts oldest unreferenced entries. */
	for (int i = 0; i < TL_FONT_CACHE_WARM + 3; i++) {
		struct tl_font_key ki = k1;
		ki.pt_tenths = 80 + i * 5;
		struct tl_font *fi = tl_font_cache_acquire(&cache, &ki, err,
			sizeof(err));
		CHECK(fi != NULL);
		tl_font_cache_release(&cache, fi);
	}
	CHECK(cache.len == TL_FONT_CACHE_WARM);

	tl_font_cache_free(&cache);
	CHECK(cache.len == 0);
}

int main(void)
{
	test_default_socket();
	test_codec_roundtrip();
	test_malformed_inputs();
	test_socket_lifecycle_and_handshake();
	test_font_cache();
	CHECK_DONE("test_ipc");
}
