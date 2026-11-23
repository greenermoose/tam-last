/* Hand-written Wayland requests for the interfaces tam-last uses, from
 * the XML files in protocols/. Each function queues one request; the
 * ones that create an object return its new id, or 0 when it could not
 * be sent. Opcodes are named after the XML. */
#ifndef TL_PROTO_H
#define TL_PROTO_H

#include "tam-last/wire.h"

/* Event opcodes. */
enum {
	TL_DISPLAY_ERROR = 0, TL_DISPLAY_DELETE_ID = 1,
	TL_REGISTRY_GLOBAL = 0, TL_REGISTRY_GLOBAL_REMOVE = 1,
	TL_CALLBACK_DONE = 0,
	TL_SURFACE_ENTER = 0, TL_SURFACE_LEAVE = 1,
	TL_SURFACE_PREFERRED_BUFFER_SCALE = 2,
	TL_SHM_FORMAT = 0,
	TL_BUFFER_RELEASE = 0,
	TL_SEAT_CAPABILITIES = 0, TL_SEAT_NAME = 1,
	TL_KEYBOARD_KEYMAP = 0, TL_KEYBOARD_ENTER = 1, TL_KEYBOARD_LEAVE = 2,
	TL_KEYBOARD_KEY = 3, TL_KEYBOARD_MODIFIERS = 4,
	TL_KEYBOARD_REPEAT_INFO = 5,
	TL_POINTER_ENTER = 0, TL_POINTER_LEAVE = 1, TL_POINTER_MOTION = 2,
	TL_POINTER_BUTTON = 3, TL_POINTER_AXIS = 4, TL_POINTER_FRAME = 5,
	TL_POINTER_AXIS_SOURCE = 6, TL_POINTER_AXIS_STOP = 7,
	TL_POINTER_AXIS_DISCRETE = 8, TL_POINTER_AXIS_VALUE120 = 9,
	TL_DATA_OFFER_OFFER = 0,
	TL_DATA_SOURCE_TARGET = 0, TL_DATA_SOURCE_SEND = 1,
	TL_DATA_SOURCE_CANCELLED = 2,
	TL_DATA_DEVICE_DATA_OFFER = 0, TL_DATA_DEVICE_SELECTION = 5,
	TL_PRIMARY_OFFER_OFFER = 0,
	TL_PRIMARY_SOURCE_SEND = 0, TL_PRIMARY_SOURCE_CANCELLED = 1,
	TL_PRIMARY_DEVICE_DATA_OFFER = 0, TL_PRIMARY_DEVICE_SELECTION = 1,
	TL_WM_BASE_PING = 0,
	TL_XDG_SURFACE_CONFIGURE = 0,
	TL_TOPLEVEL_CONFIGURE = 0, TL_TOPLEVEL_CLOSE = 1,
	TL_TOPLEVEL_CONFIGURE_BOUNDS = 2, TL_TOPLEVEL_WM_CAPABILITIES = 3,
	TL_FRACTIONAL_PREFERRED_SCALE = 0,
	TL_TEXT_INPUT_ENTER = 0, TL_TEXT_INPUT_LEAVE = 1,
	TL_TEXT_INPUT_PREEDIT_STRING = 2, TL_TEXT_INPUT_COMMIT_STRING = 3,
	TL_TEXT_INPUT_DELETE_SURROUNDING_TEXT = 4, TL_TEXT_INPUT_DONE = 5
};

/* wl_seat capability bits, wl_shm formats, cursor shapes, text-input-v3. */
#define TL_SEAT_CAP_POINTER 1u
#define TL_SEAT_CAP_KEYBOARD 2u
#define TL_SHM_XRGB8888 1u
#define TL_CURSOR_SHAPE_DEFAULT 1u
#define TL_CURSOR_SHAPE_TEXT 9u
#define TL_TEXT_INPUT_HINT_NONE 0u
#define TL_TEXT_INPUT_PURPOSE_TERMINAL 13u

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
void tl_surface_set_buffer_scale(struct tl_wire *w, uint32_t surface,
	int32_t scale);
uint32_t tl_surface_frame(struct tl_wire *w, uint32_t surface, void *data);
void tl_surface_commit(struct tl_wire *w, uint32_t surface);

uint32_t tl_shm_create_pool(struct tl_wire *w, uint32_t shm, int fd,
	int32_t size);
uint32_t tl_shm_pool_create_buffer(struct tl_wire *w, uint32_t pool,
	int32_t offset, int32_t width, int32_t height, int32_t stride,
	uint32_t format, void *data);
void tl_shm_pool_destroy(struct tl_wire *w, uint32_t pool);
void tl_buffer_destroy(struct tl_wire *w, uint32_t buffer);

uint32_t tl_seat_get_pointer(struct tl_wire *w, uint32_t seat, void *data);
void tl_pointer_release(struct tl_wire *w, uint32_t pointer);
uint32_t tl_seat_get_keyboard(struct tl_wire *w, uint32_t seat, void *data);
void tl_keyboard_release(struct tl_wire *w, uint32_t keyboard);

/* cursor-shape-v1. */
uint32_t tl_cursor_shape_get_pointer(struct tl_wire *w, uint32_t mgr,
	uint32_t pointer, void *data);
void tl_cursor_shape_set(struct tl_wire *w, uint32_t dev, uint32_t serial,
	uint32_t shape);
void tl_cursor_shape_destroy(struct tl_wire *w, uint32_t dev);

/* wl_data_device_manager: clipboard. */
uint32_t tl_data_mgr_create_source(struct tl_wire *w, uint32_t mgr, void *data);
uint32_t tl_data_mgr_get_device(struct tl_wire *w, uint32_t mgr, uint32_t seat,
	void *data);
void tl_data_device_set_selection(struct tl_wire *w, uint32_t dev,
	uint32_t source, uint32_t serial);
void tl_data_source_offer(struct tl_wire *w, uint32_t source, const char *mime);
void tl_data_source_destroy(struct tl_wire *w, uint32_t source);
void tl_data_offer_receive(struct tl_wire *w, uint32_t offer, const char *mime,
	int fd);
void tl_data_offer_destroy(struct tl_wire *w, uint32_t offer);

/* zwp_primary_selection_device_manager_v1: primary selection. */
uint32_t tl_primary_mgr_create_source(struct tl_wire *w, uint32_t mgr,
	void *data);
uint32_t tl_primary_mgr_get_device(struct tl_wire *w, uint32_t mgr,
	uint32_t seat, void *data);
void tl_primary_device_set_selection(struct tl_wire *w, uint32_t dev,
	uint32_t source, uint32_t serial);
void tl_primary_source_offer(struct tl_wire *w, uint32_t source,
	const char *mime);
void tl_primary_source_destroy(struct tl_wire *w, uint32_t source);
void tl_primary_offer_receive(struct tl_wire *w, uint32_t offer,
	const char *mime, int fd);
void tl_primary_offer_destroy(struct tl_wire *w, uint32_t offer);

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

/* viewporter: draw a buffer at a size other than its own. */
uint32_t tl_viewporter_get_viewport(struct tl_wire *w, uint32_t viewporter,
	uint32_t surface, void *data);
void tl_viewport_set_destination(struct tl_wire *w, uint32_t viewport,
	int32_t width, int32_t height);
/* fractional-scale-v1: the compositor's preferred scale, in 120ths. */
uint32_t tl_fractional_get(struct tl_wire *w, uint32_t manager,
	uint32_t surface, void *data);

/* text-input-unstable-v3: input methods. */
uint32_t tl_text_input_mgr_get(struct tl_wire *w, uint32_t mgr,
	uint32_t seat, void *data);
void tl_text_input_destroy(struct tl_wire *w, uint32_t ti);
void tl_text_input_enable(struct tl_wire *w, uint32_t ti);
void tl_text_input_disable(struct tl_wire *w, uint32_t ti);
void tl_text_input_set_content_type(struct tl_wire *w, uint32_t ti,
	uint32_t hint, uint32_t purpose);
void tl_text_input_set_cursor_rectangle(struct tl_wire *w, uint32_t ti,
	int32_t x, int32_t y, int32_t width, int32_t height);
void tl_text_input_commit(struct tl_wire *w, uint32_t ti);

#endif
