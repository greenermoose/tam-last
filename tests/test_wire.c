/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The wire format, over a socketpair: framing, strings, fd passing,
 * malformed input, id reuse, and request opcodes checked against the
 * protocol XML files. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "check.h"
#include "tam-last/proto.h"
#include "tam-last/window.h"
#include "tam-last/wire.h"

static struct tl_wire client, server;

/* Index of request (or event) name within interface iface in the XML,
 * or -1. A plain text scan: the files are well formed and ours. */
static int xml_opcode(const char *file, const char *iface, const char *kind,
	const char *name)
{
	static char buf[1 << 20];
	FILE *f = fopen(file, "r");
	if (f == NULL)
		return -2;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	char want[128];
	snprintf(want, sizeof(want), "<interface name=\"%s\"", iface);
	char *p = strstr(buf, want);
	if (p == NULL)
		return -1;
	char *end = strstr(p, "</interface>");
	char tag[32];
	snprintf(tag, sizeof(tag), "<%s name=\"", kind);
	int i = 0;
	for (char *q = strstr(p, tag); q != NULL && q < end; q = strstr(q + 1, tag), i++) {
		const char *nm = q + strlen(tag);
		if (strncmp(nm, name, strlen(name)) == 0 && nm[strlen(name)] == '"')
			return i;
	}
	return -1;
}

static int pump(struct tl_event *ev)
{
	(void)tl_wire_flush(&client);
	(void)tl_wire_read(&server);
	return tl_wire_next(&server, ev);
}

static void check_request(const char *file, const char *iface,
	const char *name, uint16_t got)
{
	int want = xml_opcode(file, iface, "request", name);
	if (want != (int)got)
		fprintf(stderr, "  %s.%s: XML says %d, we send %u\n", iface, name,
			want, got);
	CHECK(want == (int)got);
}

struct ti_rec {
	char preedit[64];
	int32_t cbeg, cend;
	int preedit_calls;
	char commit[64];
	int commit_calls;
	int order[16]; /* 1 = commit, 2 = preedit */
	int order_n;
};

static void ti_on_preedit(void *ud, const char *text, int32_t cbeg,
	int32_t cend)
{
	struct ti_rec *r = ud;
	r->preedit_calls++;
	if (text != NULL)
		snprintf(r->preedit, sizeof(r->preedit), "%s", text);
	else
		r->preedit[0] = '\0';
	r->cbeg = cbeg;
	r->cend = cend;
	if (r->order_n < 16)
		r->order[r->order_n++] = 2;
}

static void ti_on_commit(void *ud, const char *text, size_t len)
{
	struct ti_rec *r = ud;
	r->commit_calls++;
	size_t n = len < sizeof(r->commit) - 1 ? len : sizeof(r->commit) - 1;
	memcpy(r->commit, text, n);
	r->commit[n] = '\0';
	if (r->order_n < 16)
		r->order[r->order_n++] = 1;
}

static void test_text_input_lifecycle(void)
{
	int sv[2];
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	fcntl(sv[0], F_SETFL, O_NONBLOCK);
	fcntl(sv[1], F_SETFL, O_NONBLOCK);

	struct tl_wire srv;
	tl_wire_init(&srv, sv[1]);

	struct ti_rec rec;
	memset(&rec, 0, sizeof(rec));
	struct tl_window_events cbs;
	memset(&cbs, 0, sizeof(cbs));
	cbs.ime_preedit = ti_on_preedit;
	cbs.ime_commit = ti_on_commit;

	struct tl_display dpy;
	memset(&dpy, 0, sizeof(dpy));
	tl_wire_init(&dpy.wire, sv[0]);
	dpy.text_input = tl_wire_new_id(&dpy.wire, TL_IF_TEXT_INPUT, &dpy);

	struct tl_window win;
	memset(&win, 0, sizeof(win));
	win.dpy = &dpy;
	win.ev = &cbs;
	win.ud = &rec;
	win.surface = tl_wire_new_id(&dpy.wire, TL_IF_SURFACE, &win);
	CHECK(tl_window_has_text_input(&win));

	/* Server sends enter(surface). */
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_u32(&m, win.surface);
	CHECK(tl_wire_send(&srv, dpy.text_input, TL_TEXT_INPUT_ENTER, &m) == 0);
	CHECK(tl_wire_flush(&srv) == 0);
	CHECK(tl_display_dispatch(&dpy) == 0);
	CHECK(dpy.ti_focus == &win && dpy.ti_enabled &&
		dpy.ti_commit_serial == 1);

	/* Client must have sent enable, set_content_type(NONE, TERMINAL), commit. */
	CHECK(tl_display_flush(&dpy) == 0);
	CHECK(tl_wire_read(&srv) > 0);
	struct tl_event ev;
	CHECK(tl_wire_next(&srv, &ev) == 1 && ev.object == dpy.text_input && ev.opcode == 1); /* enable */
	CHECK(tl_wire_next(&srv, &ev) == 1 && ev.object == dpy.text_input && ev.opcode == 5); /* set_content_type */
	CHECK(tl_ev_u32(&ev) == TL_TEXT_INPUT_HINT_NONE);
	CHECK(tl_ev_u32(&ev) == TL_TEXT_INPUT_PURPOSE_TERMINAL);
	CHECK(tl_wire_next(&srv, &ev) == 1 && ev.object == dpy.text_input && ev.opcode == 7); /* commit */
	CHECK(tl_wire_next(&srv, &ev) == 0);

	/* Server sends preedit_string("nihao", 0, 5) + done(1). */
	tl_msg_init(&m);
	tl_msg_str(&m, "nihao");
	tl_msg_i32(&m, 0);
	tl_msg_i32(&m, 5);
	CHECK(tl_wire_send(&srv, dpy.text_input, TL_TEXT_INPUT_PREEDIT_STRING, &m) == 0);
	tl_msg_init(&m);
	tl_msg_u32(&m, 1);
	CHECK(tl_wire_send(&srv, dpy.text_input, TL_TEXT_INPUT_DONE, &m) == 0);
	CHECK(tl_wire_flush(&srv) == 0);
	CHECK(tl_display_dispatch(&dpy) == 0);
	CHECK(rec.preedit_calls == 1 && strcmp(rec.preedit, "nihao") == 0);
	CHECK(rec.cbeg == 0 && rec.cend == 5);
	CHECK(rec.commit_calls == 0);

	/* Server sends delete_surrounding_text + commit_string("你好") + done(1)
	 * with no preedit_string -> commit delivered first, then preedit cleared. */
	tl_msg_init(&m);
	tl_msg_u32(&m, 0);
	tl_msg_u32(&m, 0);
	CHECK(tl_wire_send(&srv, dpy.text_input, TL_TEXT_INPUT_DELETE_SURROUNDING_TEXT, &m) == 0);
	tl_msg_init(&m);
	tl_msg_str(&m, "\xe4\xbd\xa0\xe5\xa5\xbd");
	CHECK(tl_wire_send(&srv, dpy.text_input, TL_TEXT_INPUT_COMMIT_STRING, &m) == 0);
	tl_msg_init(&m);
	tl_msg_u32(&m, 1);
	CHECK(tl_wire_send(&srv, dpy.text_input, TL_TEXT_INPUT_DONE, &m) == 0);
	CHECK(tl_wire_flush(&srv) == 0);
	CHECK(tl_display_dispatch(&dpy) == 0);
	CHECK(rec.commit_calls == 1 && strcmp(rec.commit, "\xe4\xbd\xa0\xe5\xa5\xbd") == 0);
	CHECK(rec.preedit_calls == 2 && rec.preedit[0] == '\0');
	CHECK(rec.order_n == 3 && rec.order[1] == 1 && rec.order[2] == 2);

	/* Cursor rectangle is sent once and deduplicated when unchanged. */
	tl_window_set_cursor_rect(&win, 12, 34, 8, 16);
	tl_window_set_cursor_rect(&win, 12, 34, 8, 16);
	CHECK(dpy.ti_commit_serial == 2);
	CHECK(tl_display_flush(&dpy) == 0);
	CHECK(tl_wire_read(&srv) > 0);
	CHECK(tl_wire_next(&srv, &ev) == 1 && ev.object == dpy.text_input && ev.opcode == 6); /* set_cursor_rectangle */
	CHECK(tl_ev_i32(&ev) == 12 && tl_ev_i32(&ev) == 34 &&
		tl_ev_i32(&ev) == 8 && tl_ev_i32(&ev) == 16);
	CHECK(tl_wire_next(&srv, &ev) == 1 && ev.object == dpy.text_input && ev.opcode == 7); /* commit */
	CHECK(tl_wire_next(&srv, &ev) == 0);

	/* Preedit set, then leave(surface) clears preedit and stops sending requests. */
	tl_msg_init(&m);
	tl_msg_str(&m, "abc");
	tl_msg_i32(&m, 3);
	tl_msg_i32(&m, 3);
	CHECK(tl_wire_send(&srv, dpy.text_input, TL_TEXT_INPUT_PREEDIT_STRING, &m) == 0);
	tl_msg_init(&m);
	tl_msg_u32(&m, 2);
	CHECK(tl_wire_send(&srv, dpy.text_input, TL_TEXT_INPUT_DONE, &m) == 0);
	tl_msg_init(&m);
	tl_msg_u32(&m, win.surface);
	CHECK(tl_wire_send(&srv, dpy.text_input, TL_TEXT_INPUT_LEAVE, &m) == 0);
	CHECK(tl_wire_flush(&srv) == 0);
	CHECK(tl_display_dispatch(&dpy) == 0);
	CHECK(dpy.ti_focus == NULL && !dpy.ti_enabled);
	CHECK(rec.preedit_calls == 4 && rec.preedit[0] == '\0');
	/* Cursor rect update while unfocused does not send wire traffic. */
	tl_window_set_cursor_rect(&win, 20, 40, 8, 16);
	CHECK(dpy.ti_commit_serial == 2);

	tl_window_close(&win);
	tl_display_close(&dpy);
	tl_wire_close(&srv);
}

struct mw_rec {
	int focus_in;
	int focus_out;
	uint32_t last_key;
	int keys;
	int closes;
};

static void mw_focus_in(void *ud, const uint32_t *codes, size_t n)
{
	(void)codes;
	(void)n;
	((struct mw_rec *)ud)->focus_in++;
}

static void mw_focus_out(void *ud)
{
	((struct mw_rec *)ud)->focus_out++;
}

static void mw_key(void *ud, uint32_t code, bool pressed)
{
	struct mw_rec *r = ud;
	if (pressed) {
		r->last_key = code;
		r->keys++;
	}
}

static void mw_close(void *ud)
{
	((struct mw_rec *)ud)->closes++;
}

static void test_multi_window_routing(void)
{
	int sv[2];
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	fcntl(sv[0], F_SETFL, O_NONBLOCK);
	fcntl(sv[1], F_SETFL, O_NONBLOCK);

	struct tl_wire srv;
	tl_wire_init(&srv, sv[1]);

	struct tl_display dpy;
	memset(&dpy, 0, sizeof(dpy));
	tl_wire_init(&dpy.wire, sv[0]);
	dpy.keyboard = tl_wire_new_id(&dpy.wire, TL_IF_KEYBOARD, &dpy);

	struct tl_window_events cbs;
	memset(&cbs, 0, sizeof(cbs));
	cbs.focus_in = mw_focus_in;
	cbs.focus_out = mw_focus_out;
	cbs.key = mw_key;
	cbs.close = mw_close;

	struct mw_rec r1 = { 0 }, r2 = { 0 };
	struct tl_window w1, w2;
	memset(&w1, 0, sizeof(w1));
	memset(&w2, 0, sizeof(w2));
	w1.dpy = w2.dpy = &dpy;
	w1.ev = w2.ev = &cbs;
	w1.ud = &r1;
	w2.ud = &r2;
	w1.surface = tl_wire_new_id(&dpy.wire, TL_IF_SURFACE, &w1);
	w1.toplevel = tl_wire_new_id(&dpy.wire, TL_IF_XDG_TOPLEVEL, &w1);
	w2.surface = tl_wire_new_id(&dpy.wire, TL_IF_SURFACE, &w2);
	w2.toplevel = tl_wire_new_id(&dpy.wire, TL_IF_XDG_TOPLEVEL, &w2);

	/* Keyboard enters w1, receives key 30; then switches to w2, receives key 31. */
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_u32(&m, 1);
	tl_msg_u32(&m, w1.surface);
	tl_msg_u32(&m, 0); /* empty keys array */
	CHECK(tl_wire_send(&srv, dpy.keyboard, TL_KEYBOARD_ENTER, &m) == 0);

	tl_msg_init(&m);
	tl_msg_u32(&m, 2);
	tl_msg_u32(&m, 100);
	tl_msg_u32(&m, 30);
	tl_msg_u32(&m, 1);
	CHECK(tl_wire_send(&srv, dpy.keyboard, TL_KEYBOARD_KEY, &m) == 0);

	tl_msg_init(&m);
	tl_msg_u32(&m, 3);
	tl_msg_u32(&m, w2.surface);
	tl_msg_u32(&m, 0);
	CHECK(tl_wire_send(&srv, dpy.keyboard, TL_KEYBOARD_ENTER, &m) == 0);

	tl_msg_init(&m);
	tl_msg_u32(&m, 4);
	tl_msg_u32(&m, 101);
	tl_msg_u32(&m, 31);
	tl_msg_u32(&m, 1);
	CHECK(tl_wire_send(&srv, dpy.keyboard, TL_KEYBOARD_KEY, &m) == 0);

	CHECK(tl_wire_flush(&srv) == 0);
	CHECK(tl_display_dispatch(&dpy) == 0);
	CHECK(r1.focus_in == 1 && r1.focus_out == 1 && r1.keys == 1 && r1.last_key == 30);
	CHECK(r2.focus_in == 1 && r2.focus_out == 0 && r2.keys == 1 && r2.last_key == 31);

	/* Closing w1 leaves w2 intact and receiving toplevel.close. */
	tl_window_close(&w1);
	CHECK(dpy.kbd_focus == &w2);
	tl_msg_init(&m);
	CHECK(tl_wire_send(&srv, w2.toplevel, TL_TOPLEVEL_CLOSE, &m) == 0);
	CHECK(tl_wire_flush(&srv) == 0);
	CHECK(tl_display_dispatch(&dpy) == 0);
	CHECK(r1.closes == 0 && r2.closes == 1);

	tl_window_close(&w2);
	CHECK(dpy.kbd_focus == NULL);
	tl_display_close(&dpy);
	tl_wire_close(&srv);
}

int main(void)
{
	int sv[2];
	struct tl_event ev;
	const char *wl = "protocols/wayland.xml", *xdg = "protocols/xdg-shell.xml";
	const char *vp = "protocols/viewporter.xml";
	const char *fs = "protocols/fractional-scale-v1.xml";
	const char *cs = "protocols/cursor-shape-v1.xml";
	const char *ps = "protocols/primary-selection-unstable-v1.xml";
	const char *ti = "protocols/text-input-unstable-v3.xml";

	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	tl_wire_init(&client, sv[0]);
	tl_wire_init(&server, sv[1]);

	/* A bind: u32, string, u32, new id. */
	uint32_t id = tl_registry_bind(&client, 2, 7, "wl_seat", 5, TL_IF_SEAT,
		NULL);
	CHECK(id == 2);
	CHECK(pump(&ev) == 1);
	CHECK(ev.object == 2 && ev.opcode == 0);
	CHECK(tl_ev_u32(&ev) == 7);
	const char *s = tl_ev_str(&ev);
	CHECK(s != NULL && strcmp(s, "wl_seat") == 0);
	CHECK(tl_ev_u32(&ev) == 5);
	CHECK(tl_ev_u32(&ev) == id);
	CHECK(!ev.bad);
	(void)tl_ev_u32(&ev);
	CHECK(ev.bad); /* read past the end */

	/* A descriptor travels with its message. */
	int p[2];
	CHECK(pipe(p) == 0);
	CHECK(tl_shm_create_pool(&client, 9, p[1], 4096) != 0);
	close(p[1]);
	CHECK(pump(&ev) == 1);
	CHECK(ev.object == 9 && ev.opcode == 0);
	(void)tl_ev_u32(&ev);
	int fd = tl_ev_fd(&ev);
	CHECK(fd >= 0);
	CHECK(tl_ev_i32(&ev) == 4096);
	CHECK(write(fd, "x", 1) == 1);
	char c = 0;
	CHECK(read(p[0], &c, 1) == 1 && c == 'x');
	close(fd);
	close(p[0]);
	CHECK(tl_ev_fd(&ev) == -1 && ev.bad); /* no second fd */

	/* Malformed messages are caught. */
	uint32_t bad_len[4] = { 3, (16u << 16) | 0, 99, 0 }; /* string longer than message */
	CHECK(write(sv[0], bad_len, sizeof(bad_len)) == sizeof(bad_len));
	CHECK(tl_wire_read(&server) > 0);
	CHECK(tl_wire_next(&server, &ev) == 1);
	CHECK(tl_ev_str(&ev) == NULL && ev.bad);
	uint32_t no_nul[4] = { 3, (16u << 16) | 0, 4, 0x61616161 }; /* "aaaa" */
	CHECK(write(sv[0], no_nul, sizeof(no_nul)) == sizeof(no_nul));
	CHECK(tl_wire_read(&server) > 0);
	CHECK(tl_wire_next(&server, &ev) == 1);
	CHECK(tl_ev_str(&ev) == NULL && ev.bad);
	uint32_t tiny[2] = { 3, (4u << 16) | 0 }; /* size below the header */
	CHECK(write(sv[0], tiny, sizeof(tiny)) == sizeof(tiny));
	CHECK(tl_wire_read(&server) > 0);
	CHECK(tl_wire_next(&server, &ev) == -1);

	/* Ids are reused after delete_id, lowest first. */
	struct tl_wire w;
	tl_wire_init(&w, -1);
	CHECK(tl_wire_new_id(&w, TL_IF_CALLBACK, NULL) == 2);
	CHECK(tl_wire_new_id(&w, TL_IF_CALLBACK, NULL) == 3);
	tl_wire_zombie(&w, 2);
	CHECK(tl_wire_new_id(&w, TL_IF_CALLBACK, NULL) == 4);
	tl_wire_free_id(&w, 2);
	CHECK(tl_wire_new_id(&w, TL_IF_CALLBACK, NULL) == 2);

	/* Every request we send uses the opcode the XML gives it. */
	tl_wire_close(&server);
	tl_wire_close(&client);
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
	tl_wire_init(&client, sv[0]);
	tl_wire_init(&server, sv[1]);
#define REQ(file, iface, name, call)                                         \
	do {                                                                 \
		call;                                                        \
		if (pump(&ev) == 1)                                          \
			check_request(file, iface, name, ev.opcode);         \
		else                                                         \
			CHECK(!"no message");                                \
	} while (0)
	CHECK(pipe(p) == 0);
	REQ(wl, "wl_display", "sync", tl_display_sync(&client, NULL));
	REQ(wl, "wl_display", "get_registry", tl_display_get_registry(&client, NULL));
	REQ(wl, "wl_registry", "bind", tl_registry_bind(&client, 2, 1, "x", 1, TL_IF_SHM, NULL));
	REQ(wl, "wl_compositor", "create_surface", tl_compositor_create_surface(&client, 3, NULL));
	REQ(wl, "wl_surface", "attach", tl_surface_attach(&client, 4, 5));
	REQ(wl, "wl_surface", "damage_buffer", tl_surface_damage_buffer(&client, 4, 0, 0, 1, 1));
	REQ(wl, "wl_surface", "set_buffer_scale", tl_surface_set_buffer_scale(&client, 4, 2));
	REQ(wl, "wl_surface", "frame", tl_surface_frame(&client, 4, NULL));
	REQ(wl, "wl_surface", "commit", tl_surface_commit(&client, 4));
	REQ(wl, "wl_surface", "destroy", tl_surface_destroy(&client, 4));
	REQ(wl, "wl_shm", "create_pool", tl_shm_create_pool(&client, 3, p[0], 64));
	REQ(wl, "wl_shm_pool", "create_buffer", tl_shm_pool_create_buffer(&client, 6, 0, 1, 1, 4, 1, NULL));
	REQ(wl, "wl_shm_pool", "destroy", tl_shm_pool_destroy(&client, 6));
	REQ(wl, "wl_buffer", "destroy", tl_buffer_destroy(&client, 7));
	REQ(wl, "wl_seat", "get_pointer", tl_seat_get_pointer(&client, 3, NULL));
	REQ(wl, "wl_pointer", "release", tl_pointer_release(&client, 8));
	REQ(wl, "wl_seat", "get_keyboard", tl_seat_get_keyboard(&client, 3, NULL));
	REQ(wl, "wl_keyboard", "release", tl_keyboard_release(&client, 8));
	REQ(cs, "wp_cursor_shape_manager_v1", "get_pointer", tl_cursor_shape_get_pointer(&client, 14, 8, NULL));
	REQ(cs, "wp_cursor_shape_device_v1", "set_shape", tl_cursor_shape_set(&client, 15, 1, TL_CURSOR_SHAPE_TEXT));
	REQ(cs, "wp_cursor_shape_device_v1", "destroy", tl_cursor_shape_destroy(&client, 15));
	REQ(wl, "wl_data_device_manager", "create_data_source", tl_data_mgr_create_source(&client, 16, NULL));
	REQ(wl, "wl_data_device_manager", "get_data_device", tl_data_mgr_get_device(&client, 16, 3, NULL));
	REQ(wl, "wl_data_device", "set_selection", tl_data_device_set_selection(&client, 17, 18, 1));
	REQ(wl, "wl_data_source", "offer", tl_data_source_offer(&client, 18, "text/plain"));
	REQ(wl, "wl_data_source", "destroy", tl_data_source_destroy(&client, 18));
	REQ(wl, "wl_data_offer", "receive", tl_data_offer_receive(&client, 19, "text/plain", p[0]));
	REQ(wl, "wl_data_offer", "destroy", tl_data_offer_destroy(&client, 19));
	REQ(ps, "zwp_primary_selection_device_manager_v1", "create_source", tl_primary_mgr_create_source(&client, 20, NULL));
	REQ(ps, "zwp_primary_selection_device_manager_v1", "get_device", tl_primary_mgr_get_device(&client, 20, 3, NULL));
	REQ(ps, "zwp_primary_selection_device_v1", "set_selection", tl_primary_device_set_selection(&client, 21, 22, 1));
	REQ(ps, "zwp_primary_selection_source_v1", "offer", tl_primary_source_offer(&client, 22, "text/plain"));
	REQ(ps, "zwp_primary_selection_source_v1", "destroy", tl_primary_source_destroy(&client, 22));
	REQ(ps, "zwp_primary_selection_offer_v1", "receive", tl_primary_offer_receive(&client, 23, "text/plain", p[0]));
	REQ(ps, "zwp_primary_selection_offer_v1", "destroy", tl_primary_offer_destroy(&client, 23));
	REQ(xdg, "xdg_wm_base", "pong", tl_wm_base_pong(&client, 3, 1));
	REQ(xdg, "xdg_wm_base", "get_xdg_surface", tl_wm_base_get_xdg_surface(&client, 3, 4, NULL));
	REQ(xdg, "xdg_surface", "get_toplevel", tl_xdg_surface_get_toplevel(&client, 9, NULL));
	REQ(xdg, "xdg_surface", "ack_configure", tl_xdg_surface_ack_configure(&client, 9, 1));
	REQ(xdg, "xdg_surface", "destroy", tl_xdg_surface_destroy(&client, 9));
	REQ(xdg, "xdg_toplevel", "set_title", tl_toplevel_set_title(&client, 10, "t"));
	REQ(xdg, "xdg_toplevel", "set_app_id", tl_toplevel_set_app_id(&client, 10, "a"));
	REQ(xdg, "xdg_toplevel", "destroy", tl_toplevel_destroy(&client, 10));
	REQ(vp, "wp_viewporter", "get_viewport", tl_viewporter_get_viewport(&client, 11, 4, NULL));
	REQ(vp, "wp_viewport", "set_destination", tl_viewport_set_destination(&client, 12, 640, 480));
	REQ(vp, "wp_viewport", "destroy", tl_viewport_destroy(&client, 12));
	REQ(fs, "wp_fractional_scale_manager_v1", "get_fractional_scale", tl_fractional_get(&client, 13, 4, NULL));
	REQ(fs, "wp_fractional_scale_v1", "destroy", tl_fractional_destroy(&client, 13));
	REQ(ti, "zwp_text_input_manager_v3", "get_text_input", tl_text_input_mgr_get(&client, 24, 3, NULL));
	REQ(ti, "zwp_text_input_v3", "enable", tl_text_input_enable(&client, 25));
	REQ(ti, "zwp_text_input_v3", "disable", tl_text_input_disable(&client, 25));
	REQ(ti, "zwp_text_input_v3", "set_content_type", tl_text_input_set_content_type(&client, 25, TL_TEXT_INPUT_HINT_NONE, TL_TEXT_INPUT_PURPOSE_TERMINAL));
	REQ(ti, "zwp_text_input_v3", "set_cursor_rectangle", tl_text_input_set_cursor_rectangle(&client, 25, 10, 20, 8, 16));
	REQ(ti, "zwp_text_input_v3", "commit", tl_text_input_commit(&client, 25));
	REQ(ti, "zwp_text_input_v3", "destroy", tl_text_input_destroy(&client, 25));
	for (size_t i = 0; i < server.in_nfds; i++)
		close(server.in_fds[i]);
	server.in_nfds = 0;
	close(p[0]);
	close(p[1]);

	/* And the event opcodes we decode. */
#define EVT(file, iface, name, value) \
	CHECK(xml_opcode(file, iface, "event", name) == (value))
	EVT(wl, "wl_display", "error", TL_DISPLAY_ERROR);
	EVT(wl, "wl_display", "delete_id", TL_DISPLAY_DELETE_ID);
	EVT(wl, "wl_registry", "global", TL_REGISTRY_GLOBAL);
	EVT(wl, "wl_callback", "done", TL_CALLBACK_DONE);
	EVT(wl, "wl_surface", "preferred_buffer_scale", TL_SURFACE_PREFERRED_BUFFER_SCALE);
	EVT(wl, "wl_buffer", "release", TL_BUFFER_RELEASE);
	EVT(wl, "wl_seat", "capabilities", TL_SEAT_CAPABILITIES);
	EVT(wl, "wl_keyboard", "keymap", TL_KEYBOARD_KEYMAP);
	EVT(wl, "wl_keyboard", "enter", TL_KEYBOARD_ENTER);
	EVT(wl, "wl_keyboard", "leave", TL_KEYBOARD_LEAVE);
	EVT(wl, "wl_keyboard", "key", TL_KEYBOARD_KEY);
	EVT(wl, "wl_keyboard", "modifiers", TL_KEYBOARD_MODIFIERS);
	EVT(wl, "wl_keyboard", "repeat_info", TL_KEYBOARD_REPEAT_INFO);
	EVT(wl, "wl_pointer", "enter", TL_POINTER_ENTER);
	EVT(wl, "wl_pointer", "leave", TL_POINTER_LEAVE);
	EVT(wl, "wl_pointer", "motion", TL_POINTER_MOTION);
	EVT(wl, "wl_pointer", "button", TL_POINTER_BUTTON);
	EVT(wl, "wl_pointer", "axis", TL_POINTER_AXIS);
	EVT(wl, "wl_pointer", "frame", TL_POINTER_FRAME);
	EVT(wl, "wl_pointer", "axis_discrete", TL_POINTER_AXIS_DISCRETE);
	EVT(wl, "wl_pointer", "axis_value120", TL_POINTER_AXIS_VALUE120);
	EVT(wl, "wl_data_offer", "offer", TL_DATA_OFFER_OFFER);
	EVT(wl, "wl_data_source", "send", TL_DATA_SOURCE_SEND);
	EVT(wl, "wl_data_source", "cancelled", TL_DATA_SOURCE_CANCELLED);
	EVT(wl, "wl_data_device", "data_offer", TL_DATA_DEVICE_DATA_OFFER);
	EVT(wl, "wl_data_device", "selection", TL_DATA_DEVICE_SELECTION);
	EVT(ps, "zwp_primary_selection_offer_v1", "offer", TL_PRIMARY_OFFER_OFFER);
	EVT(ps, "zwp_primary_selection_source_v1", "send", TL_PRIMARY_SOURCE_SEND);
	EVT(ps, "zwp_primary_selection_source_v1", "cancelled", TL_PRIMARY_SOURCE_CANCELLED);
	EVT(ps, "zwp_primary_selection_device_v1", "data_offer", TL_PRIMARY_DEVICE_DATA_OFFER);
	EVT(ps, "zwp_primary_selection_device_v1", "selection", TL_PRIMARY_DEVICE_SELECTION);
	EVT(xdg, "xdg_wm_base", "ping", TL_WM_BASE_PING);
	EVT(xdg, "xdg_surface", "configure", TL_XDG_SURFACE_CONFIGURE);
	EVT(xdg, "xdg_toplevel", "configure", TL_TOPLEVEL_CONFIGURE);
	EVT(xdg, "xdg_toplevel", "close", TL_TOPLEVEL_CLOSE);
	EVT(fs, "wp_fractional_scale_v1", "preferred_scale", TL_FRACTIONAL_PREFERRED_SCALE);
	EVT(ti, "zwp_text_input_v3", "enter", TL_TEXT_INPUT_ENTER);
	EVT(ti, "zwp_text_input_v3", "leave", TL_TEXT_INPUT_LEAVE);
	EVT(ti, "zwp_text_input_v3", "preedit_string", TL_TEXT_INPUT_PREEDIT_STRING);
	EVT(ti, "zwp_text_input_v3", "commit_string", TL_TEXT_INPUT_COMMIT_STRING);
	EVT(ti, "zwp_text_input_v3", "delete_surrounding_text", TL_TEXT_INPUT_DELETE_SURROUNDING_TEXT);
	EVT(ti, "zwp_text_input_v3", "done", TL_TEXT_INPUT_DONE);

	tl_wire_close(&client);
	tl_wire_close(&server);

	test_text_input_lifecycle();
	test_multi_window_routing();
	CHECK_DONE("test_wire");
}
