/* One xdg-shell toplevel window and the seat's keyboard, on our own
 * Wayland wire code. The window reports what happens through callbacks
 * and never decides anything about the terminal. */
#ifndef TL_WINDOW_H
#define TL_WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tam-last/shm.h"
#include "tam-last/wire.h"

struct tl_window_events {
	/* Draw and present a width x height frame. 0 means "you choose". */
	void (*configure)(void *ud, int width, int height);
	void (*close)(void *ud);
	/* The compositor is ready for the next frame. */
	void (*frame)(void *ud);
	/* Raw evdev key codes, as the kernel numbers them. */
	void (*key)(void *ud, uint32_t code, bool pressed);
	void (*focus_in)(void *ud, const uint32_t *codes, size_t n);
	void (*focus_out)(void *ud);
	/* The compositor's modifier state, in XKB's real-modifier bits. */
	void (*modifiers)(void *ud, uint32_t depressed, uint32_t locked);
	void (*repeat_info)(void *ud, int32_t rate, int32_t delay);
	/* The compositor's preferred scale for this window, in 120ths:
	 * 120 is 1, 150 is 1.25. Only sent when fractional scaling works. */
	void (*scale)(void *ud, uint32_t scale120);
	/* Pointer events (x, y in logical pixels). */
	void (*pointer_enter)(void *ud, double x, double y);
	void (*pointer_leave)(void *ud);
	void (*pointer_motion)(void *ud, double x, double y);
	void (*pointer_button)(void *ud, uint32_t button, bool pressed,
		uint32_t time);
	void (*pointer_axis)(void *ud, uint32_t axis, double value,
		int32_t v120);
};

struct tl_window {
	struct tl_wire wire;
	uint32_t registry, compositor, shm, seat, wm_base;
	uint32_t viewporter, fractional_manager, viewport, fractional;
	uint32_t cursor_shape_mgr, cursor_shape_dev;
	uint32_t data_mgr, data_device;
	uint32_t primary_mgr, primary_device;
	int dest_w, dest_h; /* the viewport destination last set */
	uint32_t surface, xdg_surface, toplevel, keyboard, pointer;
	uint32_t frame_cb, sync_cb;
	uint32_t seat_version;
	uint32_t last_serial;
	uint32_t pointer_enter_serial;
	uint32_t cursor_shape;
	/* Pending pointer axis frame accumulator. */
	bool axis_pending[2];
	double axis_val[2];
	int32_t axis_v120[2];
	/* Clipboard and primary selection state. */
	uint32_t pending_clip_offer, clip_offer, clip_source;
	char pending_clip_mime[64], clip_mime[64];
	char *clip_text;
	size_t clip_len;
	uint32_t pending_prim_offer, prim_offer, prim_source;
	char pending_prim_mime[64], prim_mime[64];
	char *prim_text;
	size_t prim_len;
	bool synced;
	bool configured;
	int pending_w, pending_h;
	struct tl_buffer bufs[2];
	const struct tl_window_events *ev;
	void *ud;
	char err[200];
};

/* Connect, find the globals, and map an empty toplevel. Returns 0, or -1
 * with a message in win->err. */
int tl_window_open(struct tl_window *win, const char *title,
	const char *app_id, const struct tl_window_events *ev, void *ud);
void tl_window_close(struct tl_window *win);
int tl_window_fd(const struct tl_window *win);

/* Read and handle everything the socket has. Returns 0, or -1 when the
 * connection failed (message in win->err). */
int tl_window_dispatch(struct tl_window *win);
/* Write queued requests: 0 done, 1 socket full, -1 error. */
int tl_window_flush(struct tl_window *win);

/* A free buffer of this size, or NULL when both are still busy. */
struct tl_buffer *tl_window_buffer(struct tl_window *win, int width,
	int height);
struct tl_rect {
	int x, y, w, h;
};

/* Attach, damage all, ask for a frame callback, commit. With fractional
 * scaling, the buffer is in device pixels and is shown at the window's
 * size in logical pixels, width x height; otherwise those are ignored. */
void tl_window_present(struct tl_window *win, struct tl_buffer *b,
	int width, int height);
void tl_window_present_damage(struct tl_window *win, struct tl_buffer *b,
	int width, int height, const struct tl_rect *rects, size_t nrects);
/* True when the window can draw at a fractional scale. */
bool tl_window_can_scale(const struct tl_window *win);
bool tl_window_frame_pending(const struct tl_window *win);

void tl_window_set_cursor(struct tl_window *win, uint32_t shape);
void tl_window_set_clipboard(struct tl_window *win, const char *text,
	size_t len);
void tl_window_set_primary(struct tl_window *win, const char *text, size_t len);
/* Start receiving clipboard or primary selection into a non-blocking pipe.
 * Returns the read fd, or -1 if no selection is available. */
int tl_window_paste(struct tl_window *win, bool primary);

#endif
