/* Shared Wayland display connection and per-window xdg-shell toplevels:
 * registry, seat focus routing, clipboard/primary, IME, and frame pacing. */
#include "tam-last/window.h"

#include <signal.h>
#include <stdbool.h>
#include <sys/wait.h>
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

static struct tl_window *surface_window(const struct tl_display *dpy,
	uint32_t surface_id)
{
	const struct tl_object *obj = tl_wire_object(&dpy->wire, surface_id);
	if (obj == NULL || obj->iface != TL_IF_SURFACE)
		return NULL;
	return obj->data;
}

static void on_global(struct tl_display *dpy, struct tl_event *ev)
{
	uint32_t name = tl_ev_u32(ev);
	const char *iface = tl_ev_str(ev);
	uint32_t version = tl_ev_u32(ev);
	struct tl_wire *w = &dpy->wire;

	if (ev->bad || iface == NULL)
		return;
	if (strcmp(iface, "wl_compositor") == 0 && dpy->compositor == 0) {
		if (version >= 4) {
			dpy->compositor_version = min_u32(version, 6);
			dpy->compositor = tl_registry_bind(w, dpy->registry,
				name, iface, dpy->compositor_version,
				TL_IF_COMPOSITOR, dpy);
		}
	} else if (strcmp(iface, "wl_shm") == 0 && dpy->shm == 0) {
		dpy->shm = tl_registry_bind(w, dpy->registry, name, iface, 1,
			TL_IF_SHM, dpy);
	} else if (strcmp(iface, "wl_seat") == 0 && dpy->seat == 0) {
		dpy->seat_version = min_u32(version, 8);
		dpy->seat = tl_registry_bind(w, dpy->registry, name, iface,
			dpy->seat_version, TL_IF_SEAT, dpy);
	} else if (strcmp(iface, "xdg_wm_base") == 0 && dpy->wm_base == 0) {
		dpy->wm_base = tl_registry_bind(w, dpy->registry, name, iface,
			1, TL_IF_WM_BASE, dpy);
	} else if (strcmp(iface, "wp_viewporter") == 0 && dpy->viewporter == 0) {
		dpy->viewporter = tl_registry_bind(w, dpy->registry, name,
			iface, 1, TL_IF_VIEWPORTER, dpy);
	} else if (strcmp(iface, "wp_fractional_scale_manager_v1") == 0 &&
		   dpy->fractional_manager == 0) {
		dpy->fractional_manager = tl_registry_bind(w, dpy->registry,
			name, iface, 1, TL_IF_FRACTIONAL_MANAGER, dpy);
	} else if (strcmp(iface, "wp_cursor_shape_manager_v1") == 0 &&
		   dpy->cursor_shape_mgr == 0) {
		dpy->cursor_shape_mgr = tl_registry_bind(w, dpy->registry, name,
			iface, 1, TL_IF_CURSOR_SHAPE_MGR, dpy);
	} else if (strcmp(iface, "wl_data_device_manager") == 0 &&
		   dpy->data_mgr == 0) {
		dpy->data_mgr = tl_registry_bind(w, dpy->registry, name, iface,
			min_u32(version, 3), TL_IF_DATA_DEVICE_MGR, dpy);
	} else if (strcmp(iface, "zwp_primary_selection_device_manager_v1") == 0 &&
		   dpy->primary_mgr == 0) {
		dpy->primary_mgr = tl_registry_bind(w, dpy->registry, name,
			iface, 1, TL_IF_PRIMARY_MGR, dpy);
	} else if (strcmp(iface, "zwp_text_input_manager_v3") == 0 &&
		   dpy->text_input_mgr == 0) {
		dpy->text_input_mgr = tl_registry_bind(w, dpy->registry, name,
			iface, 1, TL_IF_TEXT_INPUT_MGR, dpy);
	}
}

static int on_display(struct tl_display *dpy, struct tl_event *ev)
{
	if (ev->opcode == TL_DISPLAY_ERROR) {
		uint32_t object = tl_ev_u32(ev);
		uint32_t code = tl_ev_u32(ev);
		const char *msg = tl_ev_str(ev);
		snprintf(dpy->err, sizeof(dpy->err),
			"compositor reported error %u on object %u: %s", code,
			object, msg != NULL ? msg : "(no message)");
		return -1;
	}
	if (ev->opcode == TL_DISPLAY_DELETE_ID) {
		uint32_t id = tl_ev_u32(ev);
		if (!ev->bad)
			tl_wire_free_id(&dpy->wire, id);
	}
	return 0;
}

static void on_keyboard(struct tl_display *dpy, struct tl_event *ev)
{
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
		(void)tl_ev_u32(ev); /* serial */
		uint32_t surf = tl_ev_u32(ev);
		const void *keys = tl_ev_array(ev, &len);
		if (ev->bad)
			break;
		struct tl_window *win = surface_window(dpy, surf);
		if (dpy->kbd_focus != NULL && dpy->kbd_focus != win &&
		    dpy->kbd_focus->ev->focus_out != NULL)
			dpy->kbd_focus->ev->focus_out(dpy->kbd_focus->ud);
		dpy->kbd_focus = win;
		if (win == NULL)
			break;
		uint32_t codes[64];
		size_t n = len / 4 < 64 ? len / 4 : 64;
		memcpy(codes, keys, n * 4);
		if (win->ev->focus_in != NULL)
			win->ev->focus_in(win->ud, codes, n);
		break;
	}
	case TL_KEYBOARD_LEAVE: {
		(void)tl_ev_u32(ev); /* serial */
		(void)tl_ev_u32(ev); /* surface */
		struct tl_window *win = dpy->kbd_focus;
		dpy->kbd_focus = NULL;
		if (!ev->bad && win != NULL && win->ev->focus_out != NULL)
			win->ev->focus_out(win->ud);
		break;
	}
	case TL_KEYBOARD_KEY: {
		uint32_t serial = tl_ev_u32(ev);
		(void)tl_ev_u32(ev); /* time */
		uint32_t code = tl_ev_u32(ev);
		uint32_t state = tl_ev_u32(ev);
		/* state 2 is "repeated" (wl_seat v10); we repeat ourselves. */
		if (!ev->bad && state != 2) {
			if (state == 1)
				dpy->last_serial = serial;
			struct tl_window *win = dpy->kbd_focus;
			if (win != NULL && win->ev->key != NULL)
				win->ev->key(win->ud, code, state == 1);
		}
		break;
	}
	case TL_KEYBOARD_MODIFIERS: {
		(void)tl_ev_u32(ev);
		uint32_t depressed = tl_ev_u32(ev);
		(void)tl_ev_u32(ev); /* latched */
		uint32_t locked = tl_ev_u32(ev);
		struct tl_window *win = dpy->kbd_focus;
		if (!ev->bad && win != NULL && win->ev->modifiers != NULL)
			win->ev->modifiers(win->ud, depressed, locked);
		break;
	}
	case TL_KEYBOARD_REPEAT_INFO: {
		int32_t rate = tl_ev_i32(ev);
		int32_t delay = tl_ev_i32(ev);
		if (!ev->bad) {
			dpy->repeat_rate = rate > 1000 ? 1000 : rate;
			dpy->repeat_delay = delay < 0 ? 0 : delay;
			struct tl_window *win = dpy->kbd_focus;
			if (win != NULL && win->ev->repeat_info != NULL)
				win->ev->repeat_info(win->ud, dpy->repeat_rate,
					dpy->repeat_delay);
		}
		break;
	}
	}
}

static void on_pointer(struct tl_display *dpy, struct tl_event *ev)
{
	switch (ev->opcode) {
	case TL_POINTER_ENTER: {
		uint32_t serial = tl_ev_u32(ev);
		uint32_t surf = tl_ev_u32(ev);
		double x = (double)tl_ev_i32(ev) / 256.0;
		double y = (double)tl_ev_i32(ev) / 256.0;
		if (ev->bad)
			break;
		struct tl_window *win = surface_window(dpy, surf);
		if (dpy->ptr_focus != NULL && dpy->ptr_focus != win &&
		    dpy->ptr_focus->ev->pointer_leave != NULL)
			dpy->ptr_focus->ev->pointer_leave(dpy->ptr_focus->ud);
		dpy->ptr_focus = win;
		dpy->pointer_enter_serial = serial;
		if (win == NULL)
			break;
		if (dpy->cursor_shape_dev != 0 && win->cursor_shape != 0)
			tl_cursor_shape_set(&dpy->wire, dpy->cursor_shape_dev,
				serial, win->cursor_shape);
		if (win->ev->pointer_enter != NULL)
			win->ev->pointer_enter(win->ud, x, y);
		break;
	}
	case TL_POINTER_LEAVE: {
		(void)tl_ev_u32(ev); /* serial */
		(void)tl_ev_u32(ev); /* surface */
		struct tl_window *win = dpy->ptr_focus;
		dpy->ptr_focus = NULL;
		dpy->pointer_enter_serial = 0;
		dpy->axis_pending[0] = dpy->axis_pending[1] = false;
		dpy->axis_val[0] = dpy->axis_val[1] = 0;
		dpy->axis_v120[0] = dpy->axis_v120[1] = 0;
		if (!ev->bad && win != NULL && win->ev->pointer_leave != NULL)
			win->ev->pointer_leave(win->ud);
		break;
	}
	case TL_POINTER_MOTION: {
		(void)tl_ev_u32(ev); /* time */
		double x = (double)tl_ev_i32(ev) / 256.0;
		double y = (double)tl_ev_i32(ev) / 256.0;
		struct tl_window *win = dpy->ptr_focus;
		if (!ev->bad && win != NULL && win->ev->pointer_motion != NULL)
			win->ev->pointer_motion(win->ud, x, y);
		break;
	}
	case TL_POINTER_BUTTON: {
		uint32_t serial = tl_ev_u32(ev);
		uint32_t time = tl_ev_u32(ev);
		uint32_t button = tl_ev_u32(ev);
		uint32_t state = tl_ev_u32(ev);
		if (!ev->bad) {
			if (state == 1)
				dpy->last_serial = serial;
			struct tl_window *win = dpy->ptr_focus;
			if (win != NULL && win->ev->pointer_button != NULL)
				win->ev->pointer_button(win->ud, button,
					state == 1, time);
		}
		break;
	}
	case TL_POINTER_AXIS: {
		(void)tl_ev_u32(ev); /* time */
		uint32_t axis = tl_ev_u32(ev);
		double val = (double)tl_ev_i32(ev) / 256.0;
		if (!ev->bad && axis < 2) {
			dpy->axis_pending[axis] = true;
			dpy->axis_val[axis] += val;
		}
		break;
	}
	case TL_POINTER_AXIS_DISCRETE: {
		uint32_t axis = tl_ev_u32(ev);
		int32_t disc = tl_ev_i32(ev);
		if (!ev->bad && axis < 2 && dpy->seat_version < 8) {
			dpy->axis_pending[axis] = true;
			dpy->axis_v120[axis] += disc * 120;
		}
		break;
	}
	case TL_POINTER_AXIS_VALUE120: {
		uint32_t axis = tl_ev_u32(ev);
		int32_t v120 = tl_ev_i32(ev);
		if (!ev->bad && axis < 2) {
			dpy->axis_pending[axis] = true;
			dpy->axis_v120[axis] += v120;
		}
		break;
	}
	case TL_POINTER_FRAME: {
		struct tl_window *win = dpy->ptr_focus;
		for (uint32_t i = 0; i < 2; i++) {
			if (!dpy->axis_pending[i])
				continue;
			if (win != NULL && win->ev->pointer_axis != NULL)
				win->ev->pointer_axis(win->ud, i,
					dpy->axis_val[i], dpy->axis_v120[i]);
			dpy->axis_pending[i] = false;
			dpy->axis_val[i] = 0;
			dpy->axis_v120[i] = 0;
		}
		break;
	}
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

/* Send a selection to the program that asked for it, never blocking the
 * terminal: write what the pipe takes now, and leave the rest to a
 * short-lived helper (double-forked, so init reaps it) that holds only this
 * descriptor and gives up after 30 seconds. */
static void write_selection_fd(int fd, const char *data, size_t len)
{
	int fl = fcntl(fd, F_GETFL);
	if (fl >= 0)
		(void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	size_t off = 0;
	bool would_block = false;
	while (off < len) {
		ssize_t n = write(fd, data + off, len - off);
		if (n > 0) {
			off += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		would_block = n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
		break;
	}
	if (would_block) {
		pid_t pid = fork();
		if (pid == 0) {
			if (fork() != 0)
				_exit(0);
			long maxfd = sysconf(_SC_OPEN_MAX);
			if (maxfd < 0 || maxfd > 65536)
				maxfd = 65536;
			for (int i = 3; i < (int)maxfd; i++)
				if (i != fd)
					(void)close(i);
			sigset_t none;
			sigemptyset(&none);
			(void)sigprocmask(SIG_SETMASK, &none, NULL);
			signal(SIGALRM, SIG_DFL);
			alarm(30);
			if (fl >= 0)
				(void)fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
			while (off < len) {
				ssize_t n = write(fd, data + off, len - off);
				if (n > 0)
					off += (size_t)n;
				else if (n < 0 && errno == EINTR)
					continue;
				else
					break;
			}
			_exit(0);
		}
		if (pid > 0)
			while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
				;
	}
	close(fd);
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

static void ti_reset_pending(struct tl_display *dpy)
{
	free(dpy->ti_pending_preedit);
	dpy->ti_pending_preedit = NULL;
	dpy->ti_pending_cursor_begin = 0;
	dpy->ti_pending_cursor_end = 0;
	dpy->ti_has_pending_preedit = false;
	free(dpy->ti_pending_commit);
	dpy->ti_pending_commit = NULL;
	dpy->ti_has_pending_commit = false;
}

static void on_text_input(struct tl_display *dpy, struct tl_event *ev)
{
	struct tl_wire *w = &dpy->wire;

	switch (ev->opcode) {
	case TL_TEXT_INPUT_ENTER: {
		uint32_t surf = tl_ev_u32(ev);
		if (ev->bad)
			break;
		struct tl_window *win = surface_window(dpy, surf);
		ti_reset_pending(dpy);
		if (dpy->ti_focus != NULL && dpy->ti_focus != win &&
		    dpy->ti_focus->ev->ime_preedit != NULL)
			dpy->ti_focus->ev->ime_preedit(dpy->ti_focus->ud, NULL,
				0, 0);
		dpy->ti_focus = win;
		if (win == NULL) {
			dpy->ti_enabled = false;
			break;
		}
		tl_text_input_enable(w, dpy->text_input);
		tl_text_input_set_content_type(w, dpy->text_input,
			TL_TEXT_INPUT_HINT_NONE, TL_TEXT_INPUT_PURPOSE_TERMINAL);
		if (win->ti_rect_valid) {
			tl_text_input_set_cursor_rectangle(w, dpy->text_input,
				win->ti_rect_x, win->ti_rect_y,
				win->ti_rect_w, win->ti_rect_h);
			win->ti_rect_sent = true;
		} else {
			win->ti_rect_sent = false;
		}
		tl_text_input_commit(w, dpy->text_input);
		dpy->ti_commit_serial++;
		dpy->ti_enabled = true;
		break;
	}
	case TL_TEXT_INPUT_LEAVE: {
		(void)tl_ev_u32(ev); /* surface */
		if (ev->bad)
			break;
		ti_reset_pending(dpy);
		struct tl_window *win = dpy->ti_focus;
		dpy->ti_focus = NULL;
		dpy->ti_enabled = false;
		if (win != NULL) {
			win->ti_rect_sent = false;
			if (win->ev->ime_preedit != NULL)
				win->ev->ime_preedit(win->ud, NULL, 0, 0);
		}
		break;
	}
	case TL_TEXT_INPUT_PREEDIT_STRING: {
		const char *text = tl_ev_str(ev);
		int32_t cbeg = tl_ev_i32(ev);
		int32_t cend = tl_ev_i32(ev);
		if (ev->bad)
			break;
		free(dpy->ti_pending_preedit);
		dpy->ti_pending_preedit = (text != NULL && text[0] != '\0') ?
			dup_text(text, strlen(text)) : NULL;
		dpy->ti_pending_cursor_begin = cbeg;
		dpy->ti_pending_cursor_end = cend;
		dpy->ti_has_pending_preedit = true;
		break;
	}
	case TL_TEXT_INPUT_COMMIT_STRING: {
		const char *text = tl_ev_str(ev);
		if (ev->bad)
			break;
		free(dpy->ti_pending_commit);
		dpy->ti_pending_commit = (text != NULL && text[0] != '\0') ?
			dup_text(text, strlen(text)) : NULL;
		dpy->ti_has_pending_commit = true;
		break;
	}
	case TL_TEXT_INPUT_DELETE_SURROUNDING_TEXT:
		(void)tl_ev_u32(ev);
		(void)tl_ev_u32(ev);
		break;
	case TL_TEXT_INPUT_DONE: {
		uint32_t serial = tl_ev_u32(ev);
		if (ev->bad)
			break;
		char *commit = dpy->ti_pending_commit;
		bool has_commit = dpy->ti_has_pending_commit;
		char *preedit = dpy->ti_pending_preedit;
		bool has_preedit = dpy->ti_has_pending_preedit;
		int32_t cbeg = dpy->ti_pending_cursor_begin;
		int32_t cend = dpy->ti_pending_cursor_end;
		dpy->ti_pending_commit = NULL;
		dpy->ti_has_pending_commit = false;
		dpy->ti_pending_preedit = NULL;
		dpy->ti_has_pending_preedit = false;
		dpy->ti_pending_cursor_begin = 0;
		dpy->ti_pending_cursor_end = 0;

		struct tl_window *win = dpy->ti_focus;
		if (win != NULL) {
			if (has_commit && commit != NULL && commit[0] != '\0' &&
			    win->ev->ime_commit != NULL)
				win->ev->ime_commit(win->ud, commit,
					strlen(commit));
			if (win->ev->ime_preedit != NULL) {
				if (has_preedit && preedit != NULL)
					win->ev->ime_preedit(win->ud, preedit,
						cbeg, cend);
				else
					win->ev->ime_preedit(win->ud, NULL, 0, 0);
			}
			if (serial == dpy->ti_commit_serial && dpy->ti_enabled &&
			    win->ti_rect_valid && !win->ti_rect_sent) {
				tl_text_input_set_cursor_rectangle(w,
					dpy->text_input, win->ti_rect_x,
					win->ti_rect_y, win->ti_rect_w,
					win->ti_rect_h);
				tl_text_input_commit(w, dpy->text_input);
				dpy->ti_commit_serial++;
				win->ti_rect_sent = true;
			}
		}
		free(commit);
		free(preedit);
		break;
	}
	default:
		break;
	}
}

static int dispatch_one(struct tl_display *dpy, struct tl_event *ev)
{
	const struct tl_object *obj = tl_wire_object(&dpy->wire, ev->object);
	struct tl_wire *w = &dpy->wire;

	if (obj == NULL)
		return 0;
	switch (obj->iface) {
	case TL_IF_DISPLAY:
		return on_display(dpy, ev);
	case TL_IF_REGISTRY:
		if (ev->opcode == TL_REGISTRY_GLOBAL)
			on_global(dpy, ev);
		break;
	case TL_IF_CALLBACK:
		if (ev->object == dpy->sync_cb) {
			dpy->sync_cb = 0;
			dpy->synced = true;
		} else if (obj->data != NULL) {
			struct tl_window *win = obj->data;
			if (ev->object == win->frame_cb) {
				win->frame_cb = 0;
				if (win->ev->frame != NULL)
					win->ev->frame(win->ud);
			}
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
			if ((caps & TL_SEAT_CAP_POINTER) && dpy->pointer == 0) {
				dpy->pointer = tl_seat_get_pointer(w, dpy->seat,
					dpy);
				if (dpy->pointer != 0 &&
				    dpy->cursor_shape_mgr != 0)
					dpy->cursor_shape_dev =
						tl_cursor_shape_get_pointer(w,
							dpy->cursor_shape_mgr,
							dpy->pointer, dpy);
			} else if (!(caps & TL_SEAT_CAP_POINTER) &&
				   dpy->pointer != 0) {
				if (dpy->cursor_shape_dev != 0) {
					tl_cursor_shape_destroy(w,
						dpy->cursor_shape_dev);
					dpy->cursor_shape_dev = 0;
				}
				if (dpy->seat_version >= 3)
					tl_pointer_release(w, dpy->pointer);
				dpy->pointer = 0;
				dpy->ptr_focus = NULL;
			}
			if ((caps & TL_SEAT_CAP_KEYBOARD) && dpy->keyboard == 0) {
				dpy->keyboard = tl_seat_get_keyboard(w,
					dpy->seat, dpy);
			} else if (!(caps & TL_SEAT_CAP_KEYBOARD) &&
				   dpy->keyboard != 0) {
				if (dpy->seat_version >= 3)
					tl_keyboard_release(w, dpy->keyboard);
				dpy->keyboard = 0;
				dpy->kbd_focus = NULL;
			}
		}
		break;
	case TL_IF_POINTER:
		if (ev->object == dpy->pointer)
			on_pointer(dpy, ev);
		break;
	case TL_IF_DATA_DEVICE:
		if (ev->opcode == TL_DATA_DEVICE_DATA_OFFER) {
			uint32_t id = tl_ev_u32(ev);
			if (!ev->bad && id != 0) {
				if (dpy->pending_clip_offer != 0)
					tl_data_offer_destroy(w,
						dpy->pending_clip_offer);
				tl_wire_server_id(w, id, TL_IF_DATA_OFFER, dpy);
				dpy->pending_clip_offer = id;
				dpy->pending_clip_mime[0] = '\0';
			}
		} else if (ev->opcode == TL_DATA_DEVICE_SELECTION) {
			uint32_t id = tl_ev_u32(ev);
			if (ev->bad)
				break;
			if (dpy->clip_offer != 0 && dpy->clip_offer != id)
				tl_data_offer_destroy(w, dpy->clip_offer);
			if (id != 0 && id == dpy->pending_clip_offer) {
				dpy->clip_offer = id;
				memcpy(dpy->clip_mime, dpy->pending_clip_mime,
					sizeof(dpy->clip_mime));
				dpy->pending_clip_offer = 0;
			} else {
				dpy->clip_offer = 0;
				dpy->clip_mime[0] = '\0';
				if (id != 0)
					tl_data_offer_destroy(w, id);
			}
		}
		break;
	case TL_IF_DATA_OFFER:
		if (ev->opcode == TL_DATA_OFFER_OFFER) {
			const char *mime = tl_ev_str(ev);
			if (!ev->bad && ev->object == dpy->pending_clip_offer)
				pick_mime(dpy->pending_clip_mime, mime);
		}
		break;
	case TL_IF_DATA_SOURCE:
		if (ev->opcode == TL_DATA_SOURCE_SEND) {
			(void)tl_ev_str(ev);
			int fd = tl_ev_fd(ev);
			if (fd >= 0) {
				if (!ev->bad && ev->object == dpy->clip_source &&
				    dpy->clip_text != NULL)
					write_selection_fd(fd, dpy->clip_text,
						dpy->clip_len);
				else
					close(fd);
			}
		} else if (ev->opcode == TL_DATA_SOURCE_CANCELLED) {
			if (ev->object == dpy->clip_source) {
				tl_data_source_destroy(w, dpy->clip_source);
				dpy->clip_source = 0;
				free(dpy->clip_text);
				dpy->clip_text = NULL;
				dpy->clip_len = 0;
			}
		}
		break;
	case TL_IF_PRIMARY_DEVICE:
		if (ev->opcode == TL_PRIMARY_DEVICE_DATA_OFFER) {
			uint32_t id = tl_ev_u32(ev);
			if (!ev->bad && id != 0) {
				if (dpy->pending_prim_offer != 0)
					tl_primary_offer_destroy(w,
						dpy->pending_prim_offer);
				tl_wire_server_id(w, id, TL_IF_PRIMARY_OFFER,
					dpy);
				dpy->pending_prim_offer = id;
				dpy->pending_prim_mime[0] = '\0';
			}
		} else if (ev->opcode == TL_PRIMARY_DEVICE_SELECTION) {
			uint32_t id = tl_ev_u32(ev);
			if (ev->bad)
				break;
			if (dpy->prim_offer != 0 && dpy->prim_offer != id)
				tl_primary_offer_destroy(w, dpy->prim_offer);
			if (id != 0 && id == dpy->pending_prim_offer) {
				dpy->prim_offer = id;
				memcpy(dpy->prim_mime, dpy->pending_prim_mime,
					sizeof(dpy->prim_mime));
				dpy->pending_prim_offer = 0;
			} else {
				dpy->prim_offer = 0;
				dpy->prim_mime[0] = '\0';
				if (id != 0)
					tl_primary_offer_destroy(w, id);
			}
		}
		break;
	case TL_IF_PRIMARY_OFFER:
		if (ev->opcode == TL_PRIMARY_OFFER_OFFER) {
			const char *mime = tl_ev_str(ev);
			if (!ev->bad && ev->object == dpy->pending_prim_offer)
				pick_mime(dpy->pending_prim_mime, mime);
		}
		break;
	case TL_IF_PRIMARY_SOURCE:
		if (ev->opcode == TL_PRIMARY_SOURCE_SEND) {
			(void)tl_ev_str(ev);
			int fd = tl_ev_fd(ev);
			if (fd >= 0) {
				if (!ev->bad && ev->object == dpy->prim_source &&
				    dpy->prim_text != NULL)
					write_selection_fd(fd, dpy->prim_text,
						dpy->prim_len);
				else
					close(fd);
			}
		} else if (ev->opcode == TL_PRIMARY_SOURCE_CANCELLED) {
			if (ev->object == dpy->prim_source) {
				tl_primary_source_destroy(w, dpy->prim_source);
				dpy->prim_source = 0;
				free(dpy->prim_text);
				dpy->prim_text = NULL;
				dpy->prim_len = 0;
			}
		}
		break;
	case TL_IF_KEYBOARD:
		if (ev->object == dpy->keyboard)
			on_keyboard(dpy, ev);
		else if (ev->opcode == TL_KEYBOARD_KEYMAP) {
			/* An old keyboard's keymap still carries an fd. */
			(void)tl_ev_u32(ev);
			int fd = tl_ev_fd(ev);
			if (fd >= 0)
				close(fd);
		}
		break;
	case TL_IF_TEXT_INPUT:
		if (ev->object == dpy->text_input)
			on_text_input(dpy, ev);
		break;
	case TL_IF_WM_BASE:
		if (ev->opcode == TL_WM_BASE_PING) {
			uint32_t serial = tl_ev_u32(ev);
			if (!ev->bad)
				tl_wm_base_pong(w, dpy->wm_base, serial);
		}
		break;
	case TL_IF_XDG_SURFACE:
		if (ev->opcode == TL_XDG_SURFACE_CONFIGURE &&
		    obj->data != NULL) {
			struct tl_window *win = obj->data;
			uint32_t serial = tl_ev_u32(ev);
			if (ev->bad)
				break;
			tl_xdg_surface_ack_configure(w, win->xdg_surface,
				serial);
			win->configured = true;
			if (win->ev->configure != NULL)
				win->ev->configure(win->ud, win->pending_w,
					win->pending_h);
		}
		break;
	case TL_IF_SURFACE:
		if (ev->opcode == TL_SURFACE_PREFERRED_BUFFER_SCALE &&
		    obj->data != NULL) {
			struct tl_window *win = obj->data;
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
		if (ev->opcode == TL_FRACTIONAL_PREFERRED_SCALE &&
		    obj->data != NULL) {
			struct tl_window *win = obj->data;
			uint32_t scale = tl_ev_u32(ev);
			if (!ev->bad && scale >= 120 && scale <= 120 * 8 &&
			    win->ev->scale != NULL)
				win->ev->scale(win->ud, scale);
		}
		break;
	case TL_IF_XDG_TOPLEVEL:
		if (obj->data == NULL)
			break;
		if (ev->opcode == TL_TOPLEVEL_CONFIGURE) {
			struct tl_window *win = obj->data;
			int32_t width = tl_ev_i32(ev);
			int32_t height = tl_ev_i32(ev);
			size_t len;
			(void)tl_ev_array(ev, &len); /* states */
			if (!ev->bad && width >= 0 && height >= 0) {
				win->pending_w = width;
				win->pending_h = height;
			}
		} else if (ev->opcode == TL_TOPLEVEL_CLOSE) {
			struct tl_window *win = obj->data;
			if (win->ev->close != NULL)
				win->ev->close(win->ud);
		}
		break;
	default:
		/* Surfaces, shm formats, zombies: nothing to do. */
		break;
	}
	if (ev->bad) {
		snprintf(dpy->err, sizeof(dpy->err),
			"malformed message from the compositor (object %u, "
			"opcode %u)", ev->object, ev->opcode);
		return -1;
	}
	return 0;
}

int tl_display_dispatch(struct tl_display *dpy)
{
	for (;;) {
		long got = tl_wire_read(&dpy->wire);
		if (got == 0) {
			snprintf(dpy->err, sizeof(dpy->err),
				"the compositor closed the connection");
			return -1;
		}
		if (got < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			snprintf(dpy->err, sizeof(dpy->err),
				"reading from the compositor: %s",
				strerror(errno));
			return -1;
		}
		struct tl_event ev;
		int r;
		while ((r = tl_wire_next(&dpy->wire, &ev)) == 1) {
			if (dispatch_one(dpy, &ev) != 0)
				return -1;
		}
		if (r < 0 || dpy->wire.failed) {
			snprintf(dpy->err, sizeof(dpy->err),
				"broken message stream from the compositor");
			return -1;
		}
	}
	return 0;
}

int tl_display_flush(struct tl_display *dpy)
{
	int r = tl_wire_flush(&dpy->wire);
	if (r < 0)
		snprintf(dpy->err, sizeof(dpy->err),
			"writing to the compositor: %s", strerror(errno));
	return r;
}

/* Block until the server has answered everything sent so far. */
static int roundtrip(struct tl_display *dpy)
{
	dpy->synced = false;
	dpy->sync_cb = tl_display_sync(&dpy->wire, NULL);
	if (dpy->sync_cb == 0)
		return -1;
	while (!dpy->synced) {
		int f = tl_display_flush(dpy);
		if (f < 0)
			return -1;
		struct pollfd p = { dpy->wire.fd, POLLIN, 0 };
		if (f > 0)
			p.events |= POLLOUT;
		if (poll(&p, 1, 5000) <= 0) {
			if (errno == EINTR)
				continue;
			snprintf(dpy->err, sizeof(dpy->err),
				"the compositor did not answer");
			return -1;
		}
		if ((p.revents & POLLIN) && tl_display_dispatch(dpy) != 0)
			return -1;
		if (p.revents & (POLLERR | POLLHUP)) {
			snprintf(dpy->err, sizeof(dpy->err),
				"the compositor closed the connection");
			return -1;
		}
	}
	return 0;
}

int tl_display_open(struct tl_display *dpy)
{
	memset(dpy, 0, sizeof(*dpy));
	dpy->wire.fd = -1;
	if (tl_wire_connect(&dpy->wire, dpy->err, sizeof(dpy->err)) != 0)
		return -1;

	struct tl_wire *w = &dpy->wire;
	dpy->registry = tl_display_get_registry(w, dpy);
	if (dpy->registry == 0 || roundtrip(dpy) != 0) {
		tl_display_close(dpy);
		return -1;
	}
	if (dpy->compositor == 0 || dpy->shm == 0 || dpy->wm_base == 0) {
		snprintf(dpy->err, sizeof(dpy->err),
			"the compositor lacks %s",
			dpy->compositor == 0 ? "wl_compositor version 4" :
			dpy->shm == 0 ? "wl_shm" : "xdg_wm_base");
		tl_display_close(dpy);
		return -1;
	}
	if (dpy->seat != 0 && dpy->data_mgr != 0)
		dpy->data_device = tl_data_mgr_get_device(w, dpy->data_mgr,
			dpy->seat, dpy);
	if (dpy->seat != 0 && dpy->primary_mgr != 0)
		dpy->primary_device = tl_primary_mgr_get_device(w,
			dpy->primary_mgr, dpy->seat, dpy);
	if (dpy->seat != 0 && dpy->text_input_mgr != 0)
		dpy->text_input = tl_text_input_mgr_get(w, dpy->text_input_mgr,
			dpy->seat, dpy);
	if (roundtrip(dpy) != 0) {
		tl_display_close(dpy);
		return -1;
	}
	return 0;
}

void tl_display_close(struct tl_display *dpy)
{
	ti_reset_pending(dpy);
	if (dpy->text_input != 0 && dpy->wire.fd >= 0) {
		tl_text_input_destroy(&dpy->wire, dpy->text_input);
		dpy->text_input = 0;
	}
	free(dpy->clip_text);
	free(dpy->prim_text);
	dpy->clip_text = dpy->prim_text = NULL;
	if (dpy->wire.fd >= 0) {
		(void)tl_wire_flush(&dpy->wire);
		tl_wire_close(&dpy->wire);
	}
}

int tl_display_fd(const struct tl_display *dpy)
{
	return dpy->wire.fd;
}

void tl_display_repeat_info(const struct tl_display *dpy, int32_t *rate,
	int32_t *delay)
{
	if (rate != NULL)
		*rate = dpy->repeat_rate;
	if (delay != NULL)
		*delay = dpy->repeat_delay;
}

int tl_window_open_on(struct tl_window *win, struct tl_display *dpy,
	const char *title, const char *app_id,
	const struct tl_window_events *ev, void *ud)
{
	memset(win, 0, sizeof(*win));
	win->dpy = dpy;
	win->buffer_scale = 1;
	win->sent_buffer_scale = 1;
	win->cursor_shape = TL_CURSOR_SHAPE_TEXT;
	win->ev = ev;
	win->ud = ud;

	struct tl_wire *w = &dpy->wire;
	win->surface = tl_compositor_create_surface(w, dpy->compositor, win);
	if (win->surface == 0) {
		snprintf(dpy->err, sizeof(dpy->err), "cannot create surface");
		return -1;
	}
	win->xdg_surface = tl_wm_base_get_xdg_surface(w, dpy->wm_base,
		win->surface, win);
	if (win->xdg_surface == 0) {
		tl_surface_destroy(w, win->surface);
		win->surface = 0;
		snprintf(dpy->err, sizeof(dpy->err), "cannot create xdg_surface");
		return -1;
	}
	win->toplevel = tl_xdg_surface_get_toplevel(w, win->xdg_surface, win);
	if (win->toplevel == 0) {
		tl_xdg_surface_destroy(w, win->xdg_surface);
		tl_surface_destroy(w, win->surface);
		win->xdg_surface = win->surface = 0;
		snprintf(dpy->err, sizeof(dpy->err), "cannot create toplevel");
		return -1;
	}
	if (dpy->viewporter != 0 && dpy->fractional_manager != 0) {
		win->viewport = tl_viewporter_get_viewport(w, dpy->viewporter,
			win->surface, win);
		win->fractional = tl_fractional_get(w, dpy->fractional_manager,
			win->surface, win);
	}
	tl_toplevel_set_title(w, win->toplevel, title);
	tl_toplevel_set_app_id(w, win->toplevel, app_id);
	/* The first commit, with no buffer, asks for the first configure. */
	tl_surface_commit(w, win->surface);
	return 0;
}

void tl_window_close(struct tl_window *win)
{
	struct tl_display *dpy = win->dpy;
	if (dpy == NULL)
		return;
	struct tl_wire *w = &dpy->wire;

	if (dpy->kbd_focus == win)
		dpy->kbd_focus = NULL;
	if (dpy->ptr_focus == win) {
		dpy->ptr_focus = NULL;
		dpy->pointer_enter_serial = 0;
	}
	if (dpy->ti_focus == win) {
		ti_reset_pending(dpy);
		if (dpy->text_input != 0 && dpy->ti_enabled && w->fd >= 0) {
			tl_text_input_disable(w, dpy->text_input);
			tl_text_input_commit(w, dpy->text_input);
			dpy->ti_commit_serial++;
		}
		dpy->ti_focus = NULL;
		dpy->ti_enabled = false;
	}
	for (int i = 0; i < 2; i++)
		tl_buffer_free(&win->bufs[i], w);
	if (win->frame_cb != 0) {
		tl_wire_zombie(w, win->frame_cb);
		win->frame_cb = 0;
	}
	if (w->fd >= 0) {
		if (win->fractional != 0)
			tl_fractional_destroy(w, win->fractional);
		if (win->viewport != 0)
			tl_viewport_destroy(w, win->viewport);
		if (win->toplevel != 0)
			tl_toplevel_destroy(w, win->toplevel);
		if (win->xdg_surface != 0)
			tl_xdg_surface_destroy(w, win->xdg_surface);
		if (win->surface != 0)
			tl_surface_destroy(w, win->surface);
	}
	memset(win, 0, sizeof(*win));
}

struct tl_buffer *tl_window_buffer(struct tl_window *win, int width,
	int height)
{
	struct tl_display *dpy = win->dpy;
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
			tl_buffer_free(b, &dpy->wire);
			if (tl_buffer_create(b, &dpy->wire, dpy->shm, width,
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
		(win->dpy != NULL && win->dpy->compositor_version >= 6);
}

void tl_window_present_damage(struct tl_window *win, struct tl_buffer *b,
	int width, int height, const struct tl_rect *rects, size_t nrects)
{
	struct tl_wire *w = &win->dpy->wire;
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
		tl_surface_damage_buffer(w, win->surface, 0, 0, b->width,
			b->height);
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

void tl_window_set_title(struct tl_window *win, const char *title)
{
	if (win->dpy == NULL || win->toplevel == 0 || title == NULL)
		return;
	tl_toplevel_set_title(&win->dpy->wire, win->toplevel, title);
}

void tl_window_set_cursor(struct tl_window *win, uint32_t shape)
{
	if (win->cursor_shape == shape)
		return;
	win->cursor_shape = shape;
	struct tl_display *dpy = win->dpy;
	if (dpy != NULL && dpy->ptr_focus == win &&
	    dpy->cursor_shape_dev != 0 && dpy->pointer_enter_serial != 0)
		tl_cursor_shape_set(&dpy->wire, dpy->cursor_shape_dev,
			dpy->pointer_enter_serial, shape);
}

void tl_window_set_clipboard(struct tl_window *win, const char *text,
	size_t len)
{
	struct tl_display *dpy = win->dpy;
	if (dpy == NULL || dpy->data_mgr == 0 || dpy->data_device == 0 ||
	    text == NULL)
		return;
	if (dpy->clip_source != 0) {
		tl_data_source_destroy(&dpy->wire, dpy->clip_source);
		dpy->clip_source = 0;
	}
	free(dpy->clip_text);
	dpy->clip_text = dup_text(text, len);
	if (dpy->clip_text == NULL)
		return;
	dpy->clip_len = len;
	dpy->clip_source = tl_data_mgr_create_source(&dpy->wire, dpy->data_mgr,
		dpy);
	if (dpy->clip_source == 0)
		return;
	tl_data_source_offer(&dpy->wire, dpy->clip_source,
		"text/plain;charset=utf-8");
	tl_data_source_offer(&dpy->wire, dpy->clip_source, "text/plain");
	tl_data_source_offer(&dpy->wire, dpy->clip_source, "UTF8_STRING");
	tl_data_device_set_selection(&dpy->wire, dpy->data_device,
		dpy->clip_source, dpy->last_serial);
}

void tl_window_set_primary(struct tl_window *win, const char *text, size_t len)
{
	struct tl_display *dpy = win->dpy;
	if (dpy == NULL || dpy->primary_mgr == 0 || dpy->primary_device == 0 ||
	    text == NULL)
		return;
	if (dpy->prim_source != 0) {
		tl_primary_source_destroy(&dpy->wire, dpy->prim_source);
		dpy->prim_source = 0;
	}
	free(dpy->prim_text);
	dpy->prim_text = dup_text(text, len);
	if (dpy->prim_text == NULL)
		return;
	dpy->prim_len = len;
	dpy->prim_source = tl_primary_mgr_create_source(&dpy->wire,
		dpy->primary_mgr, dpy);
	if (dpy->prim_source == 0)
		return;
	tl_primary_source_offer(&dpy->wire, dpy->prim_source,
		"text/plain;charset=utf-8");
	tl_primary_source_offer(&dpy->wire, dpy->prim_source, "text/plain");
	tl_primary_source_offer(&dpy->wire, dpy->prim_source, "UTF8_STRING");
	tl_primary_device_set_selection(&dpy->wire, dpy->primary_device,
		dpy->prim_source, dpy->last_serial);
}

int tl_window_paste(struct tl_window *win, bool primary)
{
	struct tl_display *dpy = win->dpy;
	if (dpy == NULL)
		return -1;
	uint32_t offer = primary ? dpy->prim_offer : dpy->clip_offer;
	const char *mime = primary ? dpy->prim_mime : dpy->clip_mime;
	if (offer == 0 || mime[0] == '\0')
		return -1;
	int p[2];
	if (pipe(p) != 0)
		return -1;
	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	if (primary)
		tl_primary_offer_receive(&dpy->wire, offer, mime, p[1]);
	else
		tl_data_offer_receive(&dpy->wire, offer, mime, p[1]);
	close(p[1]);
	return p[0];
}

void tl_window_set_cursor_rect(struct tl_window *win, int32_t x, int32_t y,
	int32_t width, int32_t height)
{
	struct tl_display *dpy = win->dpy;
	if (dpy == NULL)
		return;
	if (width < 0)
		width = 0;
	if (height < 0)
		height = 0;
	if (win->ti_rect_valid && win->ti_rect_sent &&
	    win->ti_rect_x == x && win->ti_rect_y == y &&
	    win->ti_rect_w == width && win->ti_rect_h == height)
		return;
	win->ti_rect_x = x;
	win->ti_rect_y = y;
	win->ti_rect_w = width;
	win->ti_rect_h = height;
	win->ti_rect_valid = true;
	win->ti_rect_sent = false;
	if (dpy->text_input != 0 && dpy->ti_focus == win && dpy->ti_enabled) {
		tl_text_input_set_cursor_rectangle(&dpy->wire, dpy->text_input,
			x, y, width, height);
		tl_text_input_commit(&dpy->wire, dpy->text_input);
		dpy->ti_commit_serial++;
		win->ti_rect_sent = true;
	}
}

bool tl_window_has_text_input(const struct tl_window *win)
{
	return win->dpy != NULL && win->dpy->text_input != 0;
}
