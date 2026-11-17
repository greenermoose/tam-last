/* The toplevel window: registry, xdg-shell, keyboard, frame pacing. */
#include "tam-last/window.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "tam-last/proto.h"

static uint32_t min_u32(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}

static void on_global(struct tl_window *win, struct tl_event *ev)
{
	uint32_t name = tl_ev_u32(ev);
	const char *iface = tl_ev_str(ev);
	uint32_t version = tl_ev_u32(ev);
	struct tl_wire *w = &win->wire;

	if (ev->bad || iface == NULL)
		return;
	if (strcmp(iface, "wl_compositor") == 0 && win->compositor == 0) {
		if (version >= 4)
			win->compositor = tl_registry_bind(w, win->registry, name,
				iface, 4, TL_IF_COMPOSITOR, win);
	} else if (strcmp(iface, "wl_shm") == 0 && win->shm == 0) {
		win->shm = tl_registry_bind(w, win->registry, name, iface, 1,
			TL_IF_SHM, win);
	} else if (strcmp(iface, "wl_seat") == 0 && win->seat == 0) {
		win->seat_version = min_u32(version, 5);
		win->seat = tl_registry_bind(w, win->registry, name, iface,
			win->seat_version, TL_IF_SEAT, win);
	} else if (strcmp(iface, "xdg_wm_base") == 0 && win->wm_base == 0) {
		win->wm_base = tl_registry_bind(w, win->registry, name, iface, 1,
			TL_IF_WM_BASE, win);
	}
}

static int on_display(struct tl_window *win, struct tl_event *ev)
{
	if (ev->opcode == TL_DISPLAY_ERROR) {
		uint32_t object = tl_ev_u32(ev);
		uint32_t code = tl_ev_u32(ev);
		const char *msg = tl_ev_str(ev);
		snprintf(win->err, sizeof(win->err),
			"compositor reported error %u on object %u: %s", code,
			object, msg != NULL ? msg : "(no message)");
		return -1;
	}
	if (ev->opcode == TL_DISPLAY_DELETE_ID) {
		uint32_t id = tl_ev_u32(ev);
		if (!ev->bad)
			tl_wire_free_id(&win->wire, id);
	}
	return 0;
}

static void on_keyboard(struct tl_window *win, struct tl_event *ev)
{
	const struct tl_window_events *cb = win->ev;

	switch (ev->opcode) {
	case TL_KEYBOARD_KEYMAP: {
		/* tam-last maps evdev codes with its own tables (see
		 * arch/key-path.md), so the compositor's XKB text is not read. */
		(void)tl_ev_u32(ev);
		int fd = tl_ev_fd(ev);
		if (fd >= 0)
			close(fd);
		break;
	}
	case TL_KEYBOARD_ENTER: {
		size_t len;
		(void)tl_ev_u32(ev);
		(void)tl_ev_u32(ev);
		const void *keys = tl_ev_array(ev, &len);
		if (ev->bad)
			break;
		uint32_t codes[64];
		size_t n = len / 4 < 64 ? len / 4 : 64;
		memcpy(codes, keys, n * 4);
		if (cb->focus_in != NULL)
			cb->focus_in(win->ud, codes, n);
		break;
	}
	case TL_KEYBOARD_LEAVE:
		if (cb->focus_out != NULL)
			cb->focus_out(win->ud);
		break;
	case TL_KEYBOARD_KEY: {
		(void)tl_ev_u32(ev); /* serial */
		(void)tl_ev_u32(ev); /* time */
		uint32_t code = tl_ev_u32(ev);
		uint32_t state = tl_ev_u32(ev);
		/* state 2 is "repeated" (wl_seat v10); we repeat ourselves. */
		if (!ev->bad && state != 2 && cb->key != NULL)
			cb->key(win->ud, code, state == 1);
		break;
	}
	case TL_KEYBOARD_MODIFIERS: {
		(void)tl_ev_u32(ev);
		uint32_t depressed = tl_ev_u32(ev);
		(void)tl_ev_u32(ev); /* latched */
		uint32_t locked = tl_ev_u32(ev);
		if (!ev->bad && cb->modifiers != NULL)
			cb->modifiers(win->ud, depressed, locked);
		break;
	}
	case TL_KEYBOARD_REPEAT_INFO: {
		int32_t rate = tl_ev_i32(ev);
		int32_t delay = tl_ev_i32(ev);
		if (!ev->bad && cb->repeat_info != NULL)
			cb->repeat_info(win->ud, rate, delay);
		break;
	}
	}
}

static int dispatch_one(struct tl_window *win, struct tl_event *ev)
{
	const struct tl_object *obj = tl_wire_object(&win->wire, ev->object);
	struct tl_wire *w = &win->wire;

	if (obj == NULL)
		return 0;
	switch (obj->iface) {
	case TL_IF_DISPLAY:
		return on_display(win, ev);
	case TL_IF_REGISTRY:
		if (ev->opcode == TL_REGISTRY_GLOBAL)
			on_global(win, ev);
		break;
	case TL_IF_CALLBACK:
		if (ev->object == win->sync_cb) {
			win->sync_cb = 0;
			win->synced = true;
		} else if (ev->object == win->frame_cb) {
			win->frame_cb = 0;
			if (win->ev->frame != NULL)
				win->ev->frame(win->ud);
		}
		break;
	case TL_IF_BUFFER:
		if (ev->opcode == TL_BUFFER_RELEASE && obj->data != NULL)
			((struct tl_buffer *)obj->data)->busy = false;
		break;
	case TL_IF_SEAT:
		if (ev->opcode == TL_SEAT_CAPABILITIES) {
			uint32_t caps = tl_ev_u32(ev);
			if (ev->bad)
				break;
			if ((caps & TL_SEAT_CAP_KEYBOARD) && win->keyboard == 0) {
				win->keyboard = tl_seat_get_keyboard(w, win->seat, win);
			} else if (!(caps & TL_SEAT_CAP_KEYBOARD) &&
				   win->keyboard != 0) {
				if (win->seat_version >= 3)
					tl_keyboard_release(w, win->keyboard);
				win->keyboard = 0;
			}
		}
		break;
	case TL_IF_KEYBOARD:
		if (ev->object == win->keyboard)
			on_keyboard(win, ev);
		else if (ev->opcode == TL_KEYBOARD_KEYMAP) {
			/* An old keyboard's keymap still carries an fd. */
			(void)tl_ev_u32(ev);
			int fd = tl_ev_fd(ev);
			if (fd >= 0)
				close(fd);
		}
		break;
	case TL_IF_WM_BASE:
		if (ev->opcode == TL_WM_BASE_PING) {
			uint32_t serial = tl_ev_u32(ev);
			if (!ev->bad)
				tl_wm_base_pong(w, win->wm_base, serial);
		}
		break;
	case TL_IF_XDG_SURFACE:
		if (ev->opcode == TL_XDG_SURFACE_CONFIGURE) {
			uint32_t serial = tl_ev_u32(ev);
			if (ev->bad)
				break;
			tl_xdg_surface_ack_configure(w, win->xdg_surface, serial);
			win->configured = true;
			if (win->ev->configure != NULL)
				win->ev->configure(win->ud, win->pending_w,
					win->pending_h);
		}
		break;
	case TL_IF_XDG_TOPLEVEL:
		if (ev->opcode == TL_TOPLEVEL_CONFIGURE) {
			int32_t width = tl_ev_i32(ev);
			int32_t height = tl_ev_i32(ev);
			size_t len;
			(void)tl_ev_array(ev, &len); /* states */
			if (!ev->bad && width >= 0 && height >= 0) {
				win->pending_w = width;
				win->pending_h = height;
			}
		} else if (ev->opcode == TL_TOPLEVEL_CLOSE) {
			if (win->ev->close != NULL)
				win->ev->close(win->ud);
		}
		break;
	default:
		/* Surfaces, shm formats, zombies: nothing to do. */
		break;
	}
	if (ev->bad) {
		snprintf(win->err, sizeof(win->err),
			"malformed message from the compositor (object %u, "
			"opcode %u)", ev->object, ev->opcode);
		return -1;
	}
	return 0;
}

int tl_window_dispatch(struct tl_window *win)
{
	for (;;) {
		long got = tl_wire_read(&win->wire);
		if (got == 0) {
			snprintf(win->err, sizeof(win->err),
				"the compositor closed the connection");
			return -1;
		}
		if (got < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			snprintf(win->err, sizeof(win->err),
				"reading from the compositor: %s", strerror(errno));
			return -1;
		}
		struct tl_event ev;
		int r;
		while ((r = tl_wire_next(&win->wire, &ev)) == 1) {
			if (dispatch_one(win, &ev) != 0)
				return -1;
		}
		if (r < 0 || win->wire.failed) {
			snprintf(win->err, sizeof(win->err),
				"broken message stream from the compositor");
			return -1;
		}
	}
	return 0;
}

int tl_window_flush(struct tl_window *win)
{
	int r = tl_wire_flush(&win->wire);
	if (r < 0)
		snprintf(win->err, sizeof(win->err),
			"writing to the compositor: %s", strerror(errno));
	return r;
}

/* Block until the server has answered everything sent so far. */
static int roundtrip(struct tl_window *win)
{
	win->synced = false;
	win->sync_cb = tl_display_sync(&win->wire, NULL);
	if (win->sync_cb == 0)
		return -1;
	while (!win->synced) {
		int f = tl_window_flush(win);
		if (f < 0)
			return -1;
		struct pollfd p = { win->wire.fd, POLLIN, 0 };
		if (f > 0)
			p.events |= POLLOUT;
		if (poll(&p, 1, 5000) <= 0) {
			if (errno == EINTR)
				continue;
			snprintf(win->err, sizeof(win->err),
				"the compositor did not answer");
			return -1;
		}
		if ((p.revents & POLLIN) && tl_window_dispatch(win) != 0)
			return -1;
		if (p.revents & (POLLERR | POLLHUP)) {
			snprintf(win->err, sizeof(win->err),
				"the compositor closed the connection");
			return -1;
		}
	}
	return 0;
}

int tl_window_open(struct tl_window *win, const char *title,
	const char *app_id, const struct tl_window_events *ev, void *ud)
{
	memset(win, 0, sizeof(*win));
	win->wire.fd = -1;
	win->ev = ev;
	win->ud = ud;
	if (tl_wire_connect(&win->wire, win->err, sizeof(win->err)) != 0)
		return -1;

	struct tl_wire *w = &win->wire;
	win->registry = tl_display_get_registry(w, win);
	if (win->registry == 0 || roundtrip(win) != 0)
		return -1;
	if (win->compositor == 0 || win->shm == 0 || win->wm_base == 0) {
		snprintf(win->err, sizeof(win->err),
			"the compositor lacks %s",
			win->compositor == 0 ? "wl_compositor version 4" :
			win->shm == 0 ? "wl_shm" : "xdg_wm_base");
		return -1;
	}
	win->surface = tl_compositor_create_surface(w, win->compositor, win);
	win->xdg_surface = tl_wm_base_get_xdg_surface(w, win->wm_base,
		win->surface, win);
	win->toplevel = tl_xdg_surface_get_toplevel(w, win->xdg_surface, win);
	if (win->surface == 0 || win->xdg_surface == 0 || win->toplevel == 0) {
		snprintf(win->err, sizeof(win->err), "cannot create the window");
		return -1;
	}
	tl_toplevel_set_title(w, win->toplevel, title);
	tl_toplevel_set_app_id(w, win->toplevel, app_id);
	/* The first commit, with no buffer, asks for the first configure. */
	tl_surface_commit(w, win->surface);
	if (roundtrip(win) != 0)
		return -1;
	return 0;
}

void tl_window_close(struct tl_window *win)
{
	for (int i = 0; i < 2; i++)
		tl_buffer_free(&win->bufs[i], &win->wire);
	if (win->wire.fd >= 0) {
		(void)tl_wire_flush(&win->wire);
		tl_wire_close(&win->wire);
	}
}

int tl_window_fd(const struct tl_window *win)
{
	return win->wire.fd;
}

struct tl_buffer *tl_window_buffer(struct tl_window *win, int width,
	int height)
{
	/* A free buffer of the right size first. */
	for (int i = 0; i < 2; i++) {
		struct tl_buffer *b = &win->bufs[i];
		if (b->id != 0 && !b->busy && b->width == width &&
		    b->height == height)
			return b;
	}
	/* Otherwise replace one that is free or the wrong size. */
	for (int i = 0; i < 2; i++) {
		struct tl_buffer *b = &win->bufs[i];
		if (b->id == 0 || !b->busy || b->width != width ||
		    b->height != height) {
			tl_buffer_free(b, &win->wire);
			if (tl_buffer_create(b, &win->wire, win->shm, width,
				    height) != 0)
				return NULL;
			return b;
		}
	}
	return NULL;
}

void tl_window_present(struct tl_window *win, struct tl_buffer *b)
{
	struct tl_wire *w = &win->wire;
	tl_surface_attach(w, win->surface, b->id);
	tl_surface_damage_buffer(w, win->surface, 0, 0, b->width, b->height);
	if (win->frame_cb == 0)
		win->frame_cb = tl_surface_frame(w, win->surface, win);
	tl_surface_commit(w, win->surface);
	b->busy = true;
}

bool tl_window_frame_pending(const struct tl_window *win)
{
	return win->frame_cb != 0;
}
