/* Shared Wayland display connection (globals, seat, clipboard, IME) and
 * per-window xdg-shell toplevels on our own Wayland wire code. */
#ifndef TL_WINDOW_H
#define TL_WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tam-last/shm.h"
#include "tam-last/wire.h"

struct tl_window;

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
	/* Input method (text-input-v3) events. text == NULL clears preedit. */
	void (*ime_preedit)(void *ud, const char *text, int32_t cursor_begin,
		int32_t cursor_end);
	void (*ime_commit)(void *ud, const char *text, size_t len);
};

/* One Wayland connection shared by 0..N windows in the process. */
struct tl_display {
	struct tl_wire wire;
	uint32_t registry, compositor, shm, seat, wm_base;
	uint32_t viewporter, fractional_manager;
	uint32_t cursor_shape_mgr, cursor_shape_dev;
	uint32_t data_mgr, data_device;
	uint32_t primary_mgr, primary_device;
	uint32_t text_input_mgr, text_input;
	uint32_t compositor_version;
	uint32_t seat_version;
	uint32_t keyboard, pointer;
	uint32_t sync_cb;
	bool synced;
	uint32_t last_serial;
	uint32_t pointer_enter_serial;
	int32_t repeat_rate, repeat_delay;
	/* Currently focused windows for seat devices. */
	struct tl_window *kbd_focus;
	struct tl_window *ptr_focus;
	struct tl_window *ti_focus;
	/* Pending pointer axis frame accumulator. */
	bool axis_pending[2];
	double axis_val[2];
	int32_t axis_v120[2];
	/* Seat-wide clipboard and primary selection state. */
	uint32_t pending_clip_offer, clip_offer, clip_source;
	char pending_clip_mime[64], clip_mime[64];
	char *clip_text;
	size_t clip_len;
	uint32_t pending_prim_offer, prim_offer, prim_source;
	char pending_prim_mime[64], prim_mime[64];
	char *prim_text;
	size_t prim_len;
	/* Input method (text-input-v3) double-buffered state on the seat. */
	bool ti_enabled;
	uint32_t ti_commit_serial;
	char *ti_pending_preedit;
	int32_t ti_pending_cursor_begin, ti_pending_cursor_end;
	bool ti_has_pending_preedit;
	char *ti_pending_commit;
	bool ti_has_pending_commit;
	char err[200];
};

/* One xdg-shell toplevel window attached to a tl_display. */
struct tl_window {
	struct tl_display *dpy;
	uint32_t surface, xdg_surface, toplevel;
	uint32_t viewport, fractional;
	uint32_t frame_cb;
	int dest_w, dest_h; /* the viewport destination last set */
	int32_t buffer_scale;
	int32_t sent_buffer_scale;
	uint32_t cursor_shape;
	int32_t ti_rect_x, ti_rect_y, ti_rect_w, ti_rect_h;
	bool ti_rect_valid;
	bool ti_rect_sent;
	bool configured;
	int pending_w, pending_h;
	struct tl_buffer bufs[2];
	const struct tl_window_events *ev;
	void *ud;
};

struct tl_rect {
	int x, y, w, h;
};

/* Connect to Wayland, bind globals, and create seat devices. Returns 0, or
 * -1 with a message in dpy->err. */
int tl_display_open(struct tl_display *dpy);
void tl_display_close(struct tl_display *dpy);
int tl_display_fd(const struct tl_display *dpy);
/* Read and handle everything the socket has. Returns 0, or -1 on error. */
int tl_display_dispatch(struct tl_display *dpy);
/* Write queued requests: 0 done, 1 socket full, -1 error. */
int tl_display_flush(struct tl_display *dpy);
void tl_display_repeat_info(const struct tl_display *dpy, int32_t *rate,
	int32_t *delay);

/* Map a new toplevel window on an already-open tl_display (non-blocking:
 * the compositor's configure arrives asynchronously via tl_display_dispatch). */
int tl_window_open_on(struct tl_window *win, struct tl_display *dpy,
	const char *title, const char *app_id,
	const struct tl_window_events *ev, void *ud);
/* Destroy this window's Wayland objects and buffers on its shared display. */
void tl_window_close(struct tl_window *win);

/* A free buffer of this size, or NULL when both are still busy. */
struct tl_buffer *tl_window_buffer(struct tl_window *win, int width,
	int height);

/* Attach, damage, ask for a frame callback, commit. */
void tl_window_present(struct tl_window *win, struct tl_buffer *b,
	int width, int height);
void tl_window_present_damage(struct tl_window *win, struct tl_buffer *b,
	int width, int height, const struct tl_rect *rects, size_t nrects);
bool tl_window_can_scale(const struct tl_window *win);
bool tl_window_frame_pending(const struct tl_window *win);

void tl_window_set_title(struct tl_window *win, const char *title);
void tl_window_set_cursor(struct tl_window *win, uint32_t shape);
void tl_window_set_clipboard(struct tl_window *win, const char *text,
	size_t len);
void tl_window_set_primary(struct tl_window *win, const char *text, size_t len);
/* Start receiving clipboard or primary selection into a non-blocking pipe.
 * Returns the read fd, or -1 if no selection is available. */
int tl_window_paste(struct tl_window *win, bool primary);

/* Report the cursor cell rectangle in surface-local logical pixels to an
 * active text-input-v3 input method (deduplicated). */
void tl_window_set_cursor_rect(struct tl_window *win, int32_t x, int32_t y,
	int32_t width, int32_t height);
bool tl_window_has_text_input(const struct tl_window *win);

#endif
