/* Hand-written Wayland requests for the interfaces tam-last uses, from
 * protocols/wayland.xml and protocols/xdg-shell.xml. Each function queues
 * one request; the ones that create an object return its new id, or 0
 * when it could not be sent. Opcodes are named after the XML. */
#ifndef TL_PROTO_H
#define TL_PROTO_H

#include "tam-last/wire.h"

/* Event opcodes. */
enum {
	TL_DISPLAY_ERROR = 0, TL_DISPLAY_DELETE_ID = 1,
	TL_REGISTRY_GLOBAL = 0, TL_REGISTRY_GLOBAL_REMOVE = 1,
	TL_CALLBACK_DONE = 0,
	TL_SURFACE_ENTER = 0, TL_SURFACE_LEAVE = 1,
	TL_SHM_FORMAT = 0,
	TL_BUFFER_RELEASE = 0,
	TL_SEAT_CAPABILITIES = 0, TL_SEAT_NAME = 1,
	TL_KEYBOARD_KEYMAP = 0, TL_KEYBOARD_ENTER = 1, TL_KEYBOARD_LEAVE = 2,
	TL_KEYBOARD_KEY = 3, TL_KEYBOARD_MODIFIERS = 4,
	TL_KEYBOARD_REPEAT_INFO = 5,
	TL_WM_BASE_PING = 0,
	TL_XDG_SURFACE_CONFIGURE = 0,
	TL_TOPLEVEL_CONFIGURE = 0, TL_TOPLEVEL_CLOSE = 1,
	TL_TOPLEVEL_CONFIGURE_BOUNDS = 2, TL_TOPLEVEL_WM_CAPABILITIES = 3
};

/* wl_seat capability bits and wl_shm formats. */
#define TL_SEAT_CAP_KEYBOARD 2u
#define TL_SHM_XRGB8888 1u

uint32_t tl_display_sync(struct tl_wire *w, void *data);
uint32_t tl_display_get_registry(struct tl_wire *w, void *data);
uint32_t tl_registry_bind(struct tl_wire *w, uint32_t registry,
	uint32_t name, const char *iface_name, uint32_t version,
	enum tl_iface iface, void *data);

uint32_t tl_compositor_create_surface(struct tl_wire *w, uint32_t comp,
	void *data);
void tl_surface_destroy(struct tl_wire *w, uint32_t surface);
void tl_surface_attach(struct tl_wire *w, uint32_t surface, uint32_t buffer);
void tl_surface_damage_buffer(struct tl_wire *w, uint32_t surface,
	int32_t x, int32_t y, int32_t width, int32_t height);
uint32_t tl_surface_frame(struct tl_wire *w, uint32_t surface, void *data);
void tl_surface_commit(struct tl_wire *w, uint32_t surface);

uint32_t tl_shm_create_pool(struct tl_wire *w, uint32_t shm, int fd,
	int32_t size);
uint32_t tl_shm_pool_create_buffer(struct tl_wire *w, uint32_t pool,
	int32_t offset, int32_t width, int32_t height, int32_t stride,
	uint32_t format, void *data);
void tl_shm_pool_destroy(struct tl_wire *w, uint32_t pool);
void tl_buffer_destroy(struct tl_wire *w, uint32_t buffer);

uint32_t tl_seat_get_keyboard(struct tl_wire *w, uint32_t seat, void *data);
void tl_keyboard_release(struct tl_wire *w, uint32_t keyboard);

void tl_wm_base_pong(struct tl_wire *w, uint32_t wm_base, uint32_t serial);
uint32_t tl_wm_base_get_xdg_surface(struct tl_wire *w, uint32_t wm_base,
	uint32_t surface, void *data);
uint32_t tl_xdg_surface_get_toplevel(struct tl_wire *w, uint32_t xdg_surface,
	void *data);
void tl_xdg_surface_ack_configure(struct tl_wire *w, uint32_t xdg_surface,
	uint32_t serial);
void tl_toplevel_set_title(struct tl_wire *w, uint32_t toplevel,
	const char *title);
void tl_toplevel_set_app_id(struct tl_wire *w, uint32_t toplevel,
	const char *app_id);

#endif
