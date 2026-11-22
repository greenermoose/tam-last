/* Hand-written Wayland requests. Opcodes and argument order come from
 * the XML files in protocols/; tests/test_wire.c checks every one we
 * send against them. */
#include "tam-last/proto.h"

#include <stddef.h>

/* A request that creates an object: allocate the id, send, and give the
 * id back if sending failed. */
static uint32_t send_new(struct tl_wire *w, uint32_t object, uint16_t op,
	struct tl_msg *m, uint32_t id)
{
	if (id == 0)
		return 0;
	if (tl_wire_send(w, object, op, m) != 0) {
		tl_wire_free_id(w, id);
		return 0;
	}
	return id;
}

static void send0(struct tl_wire *w, uint32_t object, uint16_t op)
{
	struct tl_msg m;
	tl_msg_init(&m);
	(void)tl_wire_send(w, object, op, &m);
}

uint32_t tl_display_sync(struct tl_wire *w, void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_CALLBACK, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	return send_new(w, 1, 0, &m, id);
}

uint32_t tl_display_get_registry(struct tl_wire *w, void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_REGISTRY, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	return send_new(w, 1, 1, &m, id);
}

uint32_t tl_registry_bind(struct tl_wire *w, uint32_t registry,
	uint32_t name, const char *iface_name, uint32_t version,
	enum tl_iface iface, void *data)
{
	/* bind's new_id has no fixed interface, so the wire carries the
	 * interface name and version before the id. */
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, iface, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, name);
	tl_msg_str(&m, iface_name);
	tl_msg_u32(&m, version);
	tl_msg_u32(&m, id);
	return send_new(w, registry, 0, &m, id);
}

uint32_t tl_compositor_create_surface(struct tl_wire *w, uint32_t comp,
	void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_SURFACE, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	return send_new(w, comp, 0, &m, id);
}

void tl_surface_destroy(struct tl_wire *w, uint32_t surface)
{
	send0(w, surface, 0);
	tl_wire_zombie(w, surface);
}

void tl_surface_attach(struct tl_wire *w, uint32_t surface, uint32_t buffer)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_u32(&m, buffer);
	tl_msg_i32(&m, 0);
	tl_msg_i32(&m, 0);
	(void)tl_wire_send(w, surface, 1, &m);
}

void tl_surface_damage_buffer(struct tl_wire *w, uint32_t surface,
	int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_i32(&m, x);
	tl_msg_i32(&m, y);
	tl_msg_i32(&m, width);
	tl_msg_i32(&m, height);
	(void)tl_wire_send(w, surface, 9, &m);
}

void tl_surface_set_buffer_scale(struct tl_wire *w, uint32_t surface,
	int32_t scale)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_i32(&m, scale);
	(void)tl_wire_send(w, surface, 8, &m);
}

uint32_t tl_surface_frame(struct tl_wire *w, uint32_t surface, void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_CALLBACK, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	return send_new(w, surface, 3, &m, id);
}

void tl_surface_commit(struct tl_wire *w, uint32_t surface)
{
	send0(w, surface, 6);
}

uint32_t tl_shm_create_pool(struct tl_wire *w, uint32_t shm, int fd,
	int32_t size)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_SHM_POOL, NULL);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	tl_msg_fd(&m, fd);
	tl_msg_i32(&m, size);
	return send_new(w, shm, 0, &m, id);
}

uint32_t tl_shm_pool_create_buffer(struct tl_wire *w, uint32_t pool,
	int32_t offset, int32_t width, int32_t height, int32_t stride,
	uint32_t format, void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_BUFFER, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	tl_msg_i32(&m, offset);
	tl_msg_i32(&m, width);
	tl_msg_i32(&m, height);
	tl_msg_i32(&m, stride);
	tl_msg_u32(&m, format);
	return send_new(w, pool, 0, &m, id);
}

void tl_shm_pool_destroy(struct tl_wire *w, uint32_t pool)
{
	send0(w, pool, 1);
	tl_wire_zombie(w, pool);
}

void tl_buffer_destroy(struct tl_wire *w, uint32_t buffer)
{
	send0(w, buffer, 0);
	tl_wire_zombie(w, buffer);
}

uint32_t tl_seat_get_pointer(struct tl_wire *w, uint32_t seat, void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_POINTER, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	return send_new(w, seat, 0, &m, id);
}

void tl_pointer_release(struct tl_wire *w, uint32_t pointer)
{
	send0(w, pointer, 1);
	tl_wire_zombie(w, pointer);
}

uint32_t tl_seat_get_keyboard(struct tl_wire *w, uint32_t seat, void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_KEYBOARD, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	return send_new(w, seat, 1, &m, id);
}

void tl_keyboard_release(struct tl_wire *w, uint32_t keyboard)
{
	/* Not a zombie: a keymap event still in flight carries an fd, which
	 * must be taken from the queue, so the id keeps its interface until
	 * delete_id. */
	send0(w, keyboard, 0);
}

uint32_t tl_cursor_shape_get_pointer(struct tl_wire *w, uint32_t mgr,
	uint32_t pointer, void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_CURSOR_SHAPE_DEV, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	tl_msg_u32(&m, pointer);
	return send_new(w, mgr, 1, &m, id);
}

void tl_cursor_shape_set(struct tl_wire *w, uint32_t dev, uint32_t serial,
	uint32_t shape)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_u32(&m, serial);
	tl_msg_u32(&m, shape);
	(void)tl_wire_send(w, dev, 1, &m);
}

void tl_cursor_shape_destroy(struct tl_wire *w, uint32_t dev)
{
	send0(w, dev, 0);
	tl_wire_zombie(w, dev);
}

uint32_t tl_data_mgr_create_source(struct tl_wire *w, uint32_t mgr, void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_DATA_SOURCE, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	return send_new(w, mgr, 0, &m, id);
}

uint32_t tl_data_mgr_get_device(struct tl_wire *w, uint32_t mgr, uint32_t seat,
	void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_DATA_DEVICE, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	tl_msg_u32(&m, seat);
	return send_new(w, mgr, 1, &m, id);
}

void tl_data_device_set_selection(struct tl_wire *w, uint32_t dev,
	uint32_t source, uint32_t serial)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_u32(&m, source);
	tl_msg_u32(&m, serial);
	(void)tl_wire_send(w, dev, 1, &m);
}

void tl_data_source_offer(struct tl_wire *w, uint32_t source, const char *mime)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_str(&m, mime);
	(void)tl_wire_send(w, source, 0, &m);
}

void tl_data_source_destroy(struct tl_wire *w, uint32_t source)
{
	send0(w, source, 1);
	tl_wire_zombie(w, source);
}

void tl_data_offer_receive(struct tl_wire *w, uint32_t offer, const char *mime,
	int fd)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_str(&m, mime);
	tl_msg_fd(&m, fd);
	(void)tl_wire_send(w, offer, 1, &m);
}

void tl_data_offer_destroy(struct tl_wire *w, uint32_t offer)
{
	send0(w, offer, 2);
	tl_wire_zombie(w, offer);
}

uint32_t tl_primary_mgr_create_source(struct tl_wire *w, uint32_t mgr,
	void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_PRIMARY_SOURCE, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	return send_new(w, mgr, 0, &m, id);
}

uint32_t tl_primary_mgr_get_device(struct tl_wire *w, uint32_t mgr,
	uint32_t seat, void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_PRIMARY_DEVICE, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	tl_msg_u32(&m, seat);
	return send_new(w, mgr, 1, &m, id);
}

void tl_primary_device_set_selection(struct tl_wire *w, uint32_t dev,
	uint32_t source, uint32_t serial)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_u32(&m, source);
	tl_msg_u32(&m, serial);
	(void)tl_wire_send(w, dev, 0, &m);
}

void tl_primary_source_offer(struct tl_wire *w, uint32_t source,
	const char *mime)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_str(&m, mime);
	(void)tl_wire_send(w, source, 0, &m);
}

void tl_primary_source_destroy(struct tl_wire *w, uint32_t source)
{
	send0(w, source, 1);
	tl_wire_zombie(w, source);
}

void tl_primary_offer_receive(struct tl_wire *w, uint32_t offer,
	const char *mime, int fd)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_str(&m, mime);
	tl_msg_fd(&m, fd);
	(void)tl_wire_send(w, offer, 0, &m);
}

void tl_primary_offer_destroy(struct tl_wire *w, uint32_t offer)
{
	send0(w, offer, 1);
	tl_wire_zombie(w, offer);
}

void tl_wm_base_pong(struct tl_wire *w, uint32_t wm_base, uint32_t serial)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_u32(&m, serial);
	(void)tl_wire_send(w, wm_base, 3, &m);
}

uint32_t tl_wm_base_get_xdg_surface(struct tl_wire *w, uint32_t wm_base,
	uint32_t surface, void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_XDG_SURFACE, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	tl_msg_u32(&m, surface);
	return send_new(w, wm_base, 2, &m, id);
}

uint32_t tl_xdg_surface_get_toplevel(struct tl_wire *w, uint32_t xdg_surface,
	void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_XDG_TOPLEVEL, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	return send_new(w, xdg_surface, 1, &m, id);
}

void tl_xdg_surface_ack_configure(struct tl_wire *w, uint32_t xdg_surface,
	uint32_t serial)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_u32(&m, serial);
	(void)tl_wire_send(w, xdg_surface, 4, &m);
}

void tl_toplevel_set_title(struct tl_wire *w, uint32_t toplevel,
	const char *title)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_str(&m, title);
	(void)tl_wire_send(w, toplevel, 2, &m);
}

void tl_toplevel_set_app_id(struct tl_wire *w, uint32_t toplevel,
	const char *app_id)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_str(&m, app_id);
	(void)tl_wire_send(w, toplevel, 3, &m);
}

uint32_t tl_viewporter_get_viewport(struct tl_wire *w, uint32_t viewporter,
	uint32_t surface, void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_VIEWPORT, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	tl_msg_u32(&m, surface);
	return send_new(w, viewporter, 1, &m, id);
}

void tl_viewport_set_destination(struct tl_wire *w, uint32_t viewport,
	int32_t width, int32_t height)
{
	struct tl_msg m;
	tl_msg_init(&m);
	tl_msg_i32(&m, width);
	tl_msg_i32(&m, height);
	(void)tl_wire_send(w, viewport, 2, &m);
}

uint32_t tl_fractional_get(struct tl_wire *w, uint32_t manager,
	uint32_t surface, void *data)
{
	struct tl_msg m;
	uint32_t id = tl_wire_new_id(w, TL_IF_FRACTIONAL_SCALE, data);
	tl_msg_init(&m);
	tl_msg_u32(&m, id);
	tl_msg_u32(&m, surface);
	return send_new(w, manager, 1, &m, id);
}
