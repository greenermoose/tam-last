/* Pixel buffers in shared memory, handed to the compositor as wl_buffers. */
#ifndef TL_SHM_H
#define TL_SHM_H

#include <stdbool.h>
#include <stdint.h>

#include "tam-last/wire.h"

struct tl_buffer {
	uint32_t id;      /* wl_buffer, 0 when unused */
	uint32_t *pixels; /* XRGB8888, stride = width */
	int width, height;
	size_t size;
	bool busy;        /* attached; the compositor still reads it */
};

/* Create a width x height buffer. Returns 0, or -1 on failure. */
int tl_buffer_create(struct tl_buffer *b, struct tl_wire *w, uint32_t shm,
	int width, int height);
void tl_buffer_free(struct tl_buffer *b, struct tl_wire *w);

#endif
