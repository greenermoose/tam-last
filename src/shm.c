/* Pixel buffers in POSIX shared memory. The object is unlinked as soon as
 * it is open, so only tam-last and the compositor ever hold it. Each
 * buffer has its own pool, destroyed straight away: the wl_buffer keeps
 * the memory alive on the compositor's side. */
#include "tam-last/shm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "tam-last/proto.h"

static int shm_anon(size_t size)
{
	static unsigned counter;
	char name[64];

	for (int tries = 0; tries < 16; tries++) {
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		snprintf(name, sizeof(name), "/tam-last-%ld-%u-%lx",
			(long)getpid(), counter++, (unsigned long)ts.tv_nsec);
		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd < 0) {
			if (errno == EEXIST)
				continue;
			return -1;
		}
		shm_unlink(name);
		if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ||
		    ftruncate(fd, (off_t)size) < 0) {
			close(fd);
			return -1;
		}
		return fd;
	}
	return -1;
}

int tl_buffer_create(struct tl_buffer *b, struct tl_wire *w, uint32_t shm,
	int width, int height)
{
	memset(b, 0, sizeof(*b));
	if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
		return -1;
	size_t stride = (size_t)width * 4;
	size_t size = stride * (size_t)height;

	int fd = shm_anon(size);
	if (fd < 0)
		return -1;
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		close(fd);
		return -1;
	}
	uint32_t pool = tl_shm_create_pool(w, shm, fd, (int32_t)size);
	close(fd); /* the wire queued its own copy */
	if (pool == 0) {
		munmap(p, size);
		return -1;
	}
	b->id = tl_shm_pool_create_buffer(w, pool, 0, width, height,
		(int32_t)stride, TL_SHM_XRGB8888, b);
	tl_shm_pool_destroy(w, pool);
	if (b->id == 0) {
		munmap(p, size);
		return -1;
	}
	b->pixels = p;
	b->width = width;
	b->height = height;
	b->size = size;
	return 0;
}

void tl_buffer_free(struct tl_buffer *b, struct tl_wire *w)
{
	if (b->id != 0)
		tl_buffer_destroy(w, b->id);
	if (b->pixels != NULL)
		munmap(b->pixels, b->size);
	memset(b, 0, sizeof(*b));
}
