/* The toplevel window: registry, xdg-shell, keyboard, frame pacing. */
#include "tam-last/window.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
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
		if (version >= 4) {
			win->compositor_version = min_u32(version, 6);
			win->compositor = tl_registry_bind(w, win->registry, name,
				iface, win->compositor_version, TL_IF_COMPOSITOR,
				win);
		}
	} else if (strcmp(iface, "wl_shm") == 0 && win->shm == 0) {
		win->shm = tl_registry_bind(w, win->registry, name, iface, 1,
			TL_IF_SHM, win);
	} else if (strcmp(iface, "wl_seat") == 0 && win->seat == 0) {
		win->seat_version = min_u32(version, 8);
		win->seat = tl_registry_bind(w, win->registry, name, iface,
			win->seat_version, TL_IF_SEAT, win);
	} else if (strcmp(iface, "xdg_wm_base") == 0 && win->wm_base == 0) {
		win->wm_base = tl_registry_bind(w, win->registry, name, iface, 1,
			TL_IF_WM_BASE, win);
	} else if (strcmp(iface, "wp_viewporter") == 0 && win->viewporter == 0) {
		win->viewporter = tl_registry_bind(w, win->registry, name, iface,
			1, TL_IF_VIEWPORTER, win);
	} else if (strcmp(iface, "wp_fractional_scale_manager_v1") == 0 &&
		   win->fractional_manager == 0) {
		win->fractional_manager = tl_registry_bind(w, win->registry, name,
			iface, 1, TL_IF_FRACTIONAL_MANAGER, win);
	} else if (strcmp(iface, "wp_cursor_shape_manager_v1") == 0 &&
		   win->cursor_shape_mgr == 0) {
		win->cursor_shape_mgr = tl_registry_bind(w, win->registry, name,
			iface, 1, TL_IF_CURSOR_SHAPE_MGR, win);
	} else if (strcmp(iface, "wl_data_device_manager") == 0 &&
		   win->data_mgr == 0) {
		win->data_mgr = tl_registry_bind(w, win->registry, name, iface,
			min_u32(version, 3), TL_IF_DATA_DEVICE_MGR, win);
	} else if (strcmp(iface, "zwp_primary_selection_device_manager_v1") == 0 &&
		   win->primary_mgr == 0) {
		win->primary_mgr = tl_registry_bind(w, win->registry, name,
			iface, 1, TL_IF_PRIMARY_MGR, win);
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
		uint32_t serial = tl_ev_u32(ev);
		(void)tl_ev_u32(ev); /* time */
		uint32_t code = tl_ev_u32(ev);
		uint32_t state = tl_ev_u32(ev);
		/* state 2 is "repeated" (wl_seat v10); we repeat ourselves. */
		if (!ev->bad && state != 2) {
			if (state == 1)
				win->last_serial = serial;
			if (cb->key != NULL)
				cb->key(win->ud, code, state == 1);
		}
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

static void on_pointer(struct tl_window *win, struct tl_event *ev)
{
	const struct tl_window_events *cb = win->ev;

	switch (ev->opcode) {
	case TL_POINTER_ENTER: {
		uint32_t serial = tl_ev_u32(ev);
		(void)tl_ev_u32(ev); /* surface */
		double x = (double)tl_ev_i32(ev) / 256.0;
		double y = (double)tl_ev_i32(ev) / 256.0;
		if (ev->bad)
			break;
		win->pointer_enter_serial = serial;
		if (win->cursor_shape_dev != 0 && win->cursor_shape != 0)
			tl_cursor_shape_set(&win->wire, win->cursor_shape_dev,
				serial, win->cursor_shape);
		if (cb->pointer_enter != NULL)
			cb->pointer_enter(win->ud, x, y);
		break;
	}
	case TL_POINTER_LEAVE:
		win->pointer_enter_serial = 0;
		if (cb->pointer_leave != NULL)
			cb->pointer_leave(win->ud);
		break;
	case TL_POINTER_MOTION: {
		(void)tl_ev_u32(ev); /* time */
		double x = (double)tl_ev_i32(ev) / 256.0;
		double y = (double)tl_ev_i32(ev) / 256.0;
		if (!ev->bad && cb->pointer_motion != NULL)
			cb->pointer_motion(win->ud, x, y);
		break;
	}
	case TL_POINTER_BUTTON: {
		uint32_t serial = tl_ev_u32(ev);
		uint32_t time = tl_ev_u32(ev);
		uint32_t button = tl_ev_u32(ev);
		uint32_t state = tl_ev_u32(ev);
		if (!ev->bad) {
			if (state == 1)
				win->last_serial = serial;
			if (cb->pointer_button != NULL)
				cb->pointer_button(win->ud, button, state == 1,
					time);
		}
		break;
	}
	case TL_POINTER_AXIS: {
		(void)tl_ev_u32(ev); /* time */
		uint32_t axis = tl_ev_u32(ev);
		double val = (double)tl_ev_i32(ev) / 256.0;
		if (!ev->bad && axis < 2) {
			win->axis_pending[axis] = true;
			win->axis_val[axis] += val;
		}
		break;
	}
	case TL_POINTER_AXIS_DISCRETE: {
		uint32_t axis = tl_ev_u32(ev);
		int32_t disc = tl_ev_i32(ev);
		if (!ev->bad && axis < 2 && win->seat_version < 8) {
			win->axis_pending[axis] = true;
			win->axis_v120[axis] += disc * 120;
		}
		break;
	}
	case TL_POINTER_AXIS_VALUE120: {
		uint32_t axis = tl_ev_u32(ev);
		int32_t v120 = tl_ev_i32(ev);
		if (!ev->bad && axis < 2) {
			win->axis_pending[axis] = true;
			win->axis_v120[axis] += v120;
		}
		break;
	}
	case TL_POINTER_FRAME:
		for (uint32_t i = 0; i < 2; i++) {
			if (!win->axis_pending[i])
				continue;
			if (cb->pointer_axis != NULL)
				cb->pointer_axis(win->ud, i, win->axis_val[i],
					win->axis_v120[i]);
			win->axis_pending[i] = false;
			win->axis_val[i] = 0;
			win->axis_v120[i] = 0;
		}
		break;
	default:
		break;
	}
}

static void pick_mime(char dst[64], const char *mime)
{
	if (mime == NULL)
		return;
	if (strcmp(mime, "text/plain;charset=utf-8") == 0)
		snprintf(dst, 64, "%s", mime);
	else if (strcmp(mime, "text/plain") == 0 &&
		 strcmp(dst, "text/plain;charset=utf-8") != 0)
		snprintf(dst, 64, "%s", mime);
	else if (strcmp(mime, "UTF8_STRING") == 0 && dst[0] == '\0')
		snprintf(dst, 64, "%s", mime);
}

static void write_selection_fd(int fd, const char *data, size_t len)
{
	size_t off = 0;
	while (off < len) {
		ssize_t n = write(fd, data + off, len - off);
		if (n > 0) {
			off += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		break;
	}
	close(fd);
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
			if ((caps & TL_SEAT_CAP_POINTER) && win->pointer == 0) {
				win->pointer = tl_seat_get_pointer(w, win->seat, win);
				if (win->pointer != 0 && win->cursor_shape_mgr != 0)
					win->cursor_shape_dev = tl_cursor_shape_get_pointer(
						w, win->cursor_shape_mgr, win->pointer, win);
			} else if (!(caps & TL_SEAT_CAP_POINTER) &&
				   win->pointer != 0) {
				if (win->cursor_shape_dev != 0) {
					tl_cursor_shape_destroy(w, win->cursor_shape_dev);
					win->cursor_shape_dev = 0;
				}
				if (win->seat_version >= 3)
					tl_pointer_release(w, win->pointer);
				win->pointer = 0;
			}
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
	case TL_IF_POINTER:
		if (ev->object == win->pointer)
			on_pointer(win, ev);
		break;
	case TL_IF_DATA_DEVICE:
		if (ev->opcode == TL_DATA_DEVICE_DATA_OFFER) {
			uint32_t id = tl_ev_u32(ev);
			if (!ev->bad && id != 0) {
				if (win->pending_clip_offer != 0)
					tl_data_offer_destroy(w, win->pending_clip_offer);
				tl_wire_server_id(w, id, TL_IF_DATA_OFFER, win);
				win->pending_clip_offer = id;
				win->pending_clip_mime[0] = '\0';
			}
		} else if (ev->opcode == TL_DATA_DEVICE_SELECTION) {
			uint32_t id = tl_ev_u32(ev);
			if (ev->bad)
				break;
			if (win->clip_offer != 0 && win->clip_offer != id)
				tl_data_offer_destroy(w, win->clip_offer);
			if (id != 0 && id == win->pending_clip_offer) {
				win->clip_offer = id;
				memcpy(win->clip_mime, win->pending_clip_mime,
					sizeof(win->clip_mime));
				win->pending_clip_offer = 0;
			} else {
				win->clip_offer = 0;
				win->clip_mime[0] = '\0';
				if (id != 0)
					tl_data_offer_destroy(w, id);
			}
		}
		break;
	case TL_IF_DATA_OFFER:
		if (ev->opcode == TL_DATA_OFFER_OFFER) {
			const char *mime = tl_ev_str(ev);
			if (!ev->bad && ev->object == win->pending_clip_offer)
				pick_mime(win->pending_clip_mime, mime);
		}
		break;
	case TL_IF_DATA_SOURCE:
		if (ev->opcode == TL_DATA_SOURCE_SEND) {
			(void)tl_ev_str(ev);
			int fd = tl_ev_fd(ev);
			if (fd >= 0) {
				if (!ev->bad && ev->object == win->clip_source &&
				    win->clip_text != NULL)
					write_selection_fd(fd, win->clip_text,
						win->clip_len);
				else
					close(fd);
			}
		} else if (ev->opcode == TL_DATA_SOURCE_CANCELLED) {
			if (ev->object == win->clip_source) {
				tl_data_source_destroy(w, win->clip_source);
				win->clip_source = 0;
				free(win->clip_text);
				win->clip_text = NULL;
				win->clip_len = 0;
			}
		}
		break;
	case TL_IF_PRIMARY_DEVICE:
		if (ev->opcode == TL_PRIMARY_DEVICE_DATA_OFFER) {
			uint32_t id = tl_ev_u32(ev);
			if (!ev->bad && id != 0) {
				if (win->pending_prim_offer != 0)
					tl_primary_offer_destroy(w, win->pending_prim_offer);
				tl_wire_server_id(w, id, TL_IF_PRIMARY_OFFER, win);
				win->pending_prim_offer = id;
				win->pending_prim_mime[0] = '\0';
			}
		} else if (ev->opcode == TL_PRIMARY_DEVICE_SELECTION) {
			uint32_t id = tl_ev_u32(ev);
			if (ev->bad)
				break;
			if (win->prim_offer != 0 && win->prim_offer != id)
				tl_primary_offer_destroy(w, win->prim_offer);
			if (id != 0 && id == win->pending_prim_offer) {
				win->prim_offer = id;
				memcpy(win->prim_mime, win->pending_prim_mime,
					sizeof(win->prim_mime));
				win->pending_prim_offer = 0;
			} else {
				win->prim_offer = 0;
				win->prim_mime[0] = '\0';
				if (id != 0)
					tl_primary_offer_destroy(w, id);
			}
		}
		break;
	case TL_IF_PRIMARY_OFFER:
		if (ev->opcode == TL_PRIMARY_OFFER_OFFER) {
			const char *mime = tl_ev_str(ev);
			if (!ev->bad && ev->object == win->pending_prim_offer)
				pick_mime(win->pending_prim_mime, mime);
		}
		break;
	case TL_IF_PRIMARY_SOURCE:
		if (ev->opcode == TL_PRIMARY_SOURCE_SEND) {
			(void)tl_ev_str(ev);
			int fd = tl_ev_fd(ev);
			if (fd >= 0) {
				if (!ev->bad && ev->object == win->prim_source &&
				    win->prim_text != NULL)
					write_selection_fd(fd, win->prim_text,
						win->prim_len);
				else
					close(fd);
			}
		} else if (ev->opcode == TL_PRIMARY_SOURCE_CANCELLED) {
			if (ev->object == win->prim_source) {
				tl_primary_source_destroy(w, win->prim_source);
				win->prim_source = 0;
				free(win->prim_text);
				win->prim_text = NULL;
				win->prim_len = 0;
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
	case TL_IF_SURFACE:
		if (ev->opcode == TL_SURFACE_PREFERRED_BUFFER_SCALE) {
			int32_t factor = tl_ev_i32(ev);
			if (!ev->bad && factor >= 1 && factor <= 8 &&
			    (win->viewport == 0 || win->fractional == 0)) {
				win->buffer_scale = factor;
				if (win->ev->scale != NULL)
					win->ev->scale(win->ud,
						(uint32_t)factor * 120u);
			}
		}
		break;
	case TL_IF_FRACTIONAL_SCALE:
		if (ev->opcode == TL_FRACTIONAL_PREFERRED_SCALE) {
			uint32_t scale = tl_ev_u32(ev);
			if (!ev->bad && scale >= 120 && scale <= 120 * 8 &&
			    win->ev->scale != NULL)
				win->ev->scale(win->ud, scale);
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
	win->buffer_scale = 1;
	win->sent_buffer_scale = 1;
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
	/* Fractional scaling needs both: the compositor says the scale, and
	 * the viewport shows our larger buffer at the window's size. */
	if (win->viewporter != 0 && win->fractional_manager != 0) {
		win->viewport = tl_viewporter_get_viewport(w, win->viewporter,
			win->surface, win);
		win->fractional = tl_fractional_get(w, win->fractional_manager,
			win->surface, win);
	}
	if (win->seat != 0 && win->data_mgr != 0)
		win->data_device = tl_data_mgr_get_device(w, win->data_mgr,
			win->seat, win);
	if (win->seat != 0 && win->primary_mgr != 0)
		win->primary_device = tl_primary_mgr_get_device(w,
			win->primary_mgr, win->seat, win);
	win->cursor_shape = TL_CURSOR_SHAPE_TEXT;
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
	free(win->clip_text);
	free(win->prim_text);
	win->clip_text = win->prim_text = NULL;
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

bool tl_window_can_scale(const struct tl_window *win)
{
	return (win->viewport != 0 && win->fractional != 0) ||
		win->compositor_version >= 6;
}

void tl_window_present_damage(struct tl_window *win, struct tl_buffer *b,
	int width, int height, const struct tl_rect *rects, size_t nrects)
{
	struct tl_wire *w = &win->wire;
	if (win->viewport != 0) {
		if (width != win->dest_w || height != win->dest_h) {
			tl_viewport_set_destination(w, win->viewport, width,
				height);
			win->dest_w = width;
			win->dest_h = height;
		}
	} else if (win->buffer_scale != win->sent_buffer_scale) {
		tl_surface_set_buffer_scale(w, win->surface, win->buffer_scale);
		win->sent_buffer_scale = win->buffer_scale;
	}
	tl_surface_attach(w, win->surface, b->id);
	if (nrects == 0) {
		tl_surface_damage_buffer(w, win->surface, 0, 0, b->width, b->height);
	} else {
		for (size_t i = 0; i < nrects; i++)
			tl_surface_damage_buffer(w, win->surface, rects[i].x,
				rects[i].y, rects[i].w, rects[i].h);
	}
	if (win->frame_cb == 0)
		win->frame_cb = tl_surface_frame(w, win->surface, win);
	tl_surface_commit(w, win->surface);
	b->busy = true;
}

void tl_window_present(struct tl_window *win, struct tl_buffer *b,
	int width, int height)
{
	tl_window_present_damage(win, b, width, height, NULL, 0);
}

bool tl_window_frame_pending(const struct tl_window *win)
{
	return win->frame_cb != 0;
}

void tl_window_set_cursor(struct tl_window *win, uint32_t shape)
{
	if (win->cursor_shape == shape)
		return;
	win->cursor_shape = shape;
	if (win->cursor_shape_dev != 0 && win->pointer_enter_serial != 0)
		tl_cursor_shape_set(&win->wire, win->cursor_shape_dev,
			win->pointer_enter_serial, shape);
}

static char *dup_text(const char *text, size_t len)
{
	char *p = malloc(len + 1);
	if (p == NULL)
		return NULL;
	if (len > 0)
		memcpy(p, text, len);
	p[len] = '\0';
	return p;
}

void tl_window_set_clipboard(struct tl_window *win, const char *text,
	size_t len)
{
	if (win->data_mgr == 0 || win->data_device == 0 || text == NULL)
		return;
	if (win->clip_source != 0) {
		tl_data_source_destroy(&win->wire, win->clip_source);
		win->clip_source = 0;
	}
	free(win->clip_text);
	win->clip_text = dup_text(text, len);
	if (win->clip_text == NULL)
		return;
	win->clip_len = len;
	win->clip_source = tl_data_mgr_create_source(&win->wire, win->data_mgr,
		win);
	if (win->clip_source == 0)
		return;
	tl_data_source_offer(&win->wire, win->clip_source,
		"text/plain;charset=utf-8");
	tl_data_source_offer(&win->wire, win->clip_source, "text/plain");
	tl_data_source_offer(&win->wire, win->clip_source, "UTF8_STRING");
	tl_data_device_set_selection(&win->wire, win->data_device,
		win->clip_source, win->last_serial);
}

void tl_window_set_primary(struct tl_window *win, const char *text, size_t len)
{
	if (win->primary_mgr == 0 || win->primary_device == 0 || text == NULL)
		return;
	if (win->prim_source != 0) {
		tl_primary_source_destroy(&win->wire, win->prim_source);
		win->prim_source = 0;
	}
	free(win->prim_text);
	win->prim_text = dup_text(text, len);
	if (win->prim_text == NULL)
		return;
	win->prim_len = len;
	win->prim_source = tl_primary_mgr_create_source(&win->wire,
		win->primary_mgr, win);
	if (win->prim_source == 0)
		return;
	tl_primary_source_offer(&win->wire, win->prim_source,
		"text/plain;charset=utf-8");
	tl_primary_source_offer(&win->wire, win->prim_source, "text/plain");
	tl_primary_source_offer(&win->wire, win->prim_source, "UTF8_STRING");
	tl_primary_device_set_selection(&win->wire, win->primary_device,
		win->prim_source, win->last_serial);
}

int tl_window_paste(struct tl_window *win, bool primary)
{
	uint32_t offer = primary ? win->prim_offer : win->clip_offer;
	const char *mime = primary ? win->prim_mime : win->clip_mime;
	if (offer == 0 || mime[0] == '\0')
		return -1;
	int p[2];
	if (pipe(p) != 0)
		return -1;
	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	if (primary)
		tl_primary_offer_receive(&win->wire, offer, mime, p[1]);
	else
		tl_data_offer_receive(&win->wire, offer, mime, p[1]);
	close(p[1]);
	return p[0];
}
