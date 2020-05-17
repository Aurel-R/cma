#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <errno.h>
#include "cma.h"

#define UNUSED	__attribute__((unused))

#if !defined(__LP64__) && !defined(_LP64)
#error "LP64 is required."
#endif
#if INTPTR_MAX != INT64_MAX || UINTPTR_MAX != UINT64_MAX
#error "incompatible types (u)intptr_t (u)int64_t."
#endif

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#ifndef min
#define min(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef max
#define max(x, y) ((x) > (y) ? (x) : (y))
#endif

#define ALIGN(x, align)	(((x) + (align) - 1) & ~((align) - 1))

struct cm_info {
	size_t bm_offset;
	size_t bm_length;
	uintptr_t base_addr;
	uintptr_t root;
};

#define bm_addr_set(b, p, bm)	bitmap_set((uintptr_t)(b), (uintptr_t)(p), bm) 
#define bm_addr_unset(b, p, bm)	bitmap_unset((uintptr_t)(b), (uintptr_t)(p), bm)
#define bm_addr_isset(b, p, bm)	bitmap_isset((uintptr_t)(b), (uintptr_t)(p), bm)

static inline void bitmap_set(uintptr_t b, uintptr_t p, uint64_t bm[static 8])
{
	size_t k = (p - b) >> 9;
	uintptr_t bk = b + (k << 9);
	uint64_t mask = 1ull << ((p - bk) >> 3);
	bm[k] |= mask;
}

static inline void bitmap_unset(uintptr_t b, uintptr_t p, uint64_t bm[static 8])
{
	size_t k = (p - b) >> 9;
	uintptr_t bk = b + (k << 9);
	uint64_t mask = 1ull << ((p - bk) >> 3);
	bm[k] &= ~mask;
}

static inline uint64_t bitmap_isset(uintptr_t b, uintptr_t p, uint64_t bm[static 8])
{
	size_t k = (p - b) >> 9;
	uintptr_t bk = b + (k << 9);
	uint64_t mask = 1ull << ((p - bk) >> 3);
	return bm[k] & mask;
}

static int subptr_64(uintptr_t p, uintptr_t q, int64_t *out) 
{
	uint64_t diff = max(p, q) - min(p, q);

	if (diff > (uint64_t)INT64_MAX)
		return -1;

	*out = p < q ? -diff : diff;
	return 0;
}

static int cm_grow(struct cm_attr *mem, size_t size)
{
	size_t new_file_size = ALIGN(size, mem->pagesize);

	if (mem->fd == -1 || size <= mem->file_size)
		return 0;

	if (unlikely(ftruncate(mem->fd, new_file_size)))
		return -1;

	mem->file_size = new_file_size;
	return 0;
}

static int cm_truncate(struct cm_attr *mem, size_t size)
{
	if (mem->fd == -1) 
		return 0;

	if (unlikely(ftruncate(mem->fd, size)))
		return -1;

	mem->file_size = size;
	return 0;
}

struct cm_attr *cm_create(int fd, size_t length, int mode)
{
	void *addr;
	struct cm_attr *mem = malloc(sizeof(struct cm_attr));
	const ssize_t pagesize = (ssize_t)sysconf(_SC_PAGESIZE);

	if (!mem || pagesize < 0) {
		free(mem);
		return NULL;
	}

	length = ALIGN(length, (size_t)pagesize);
	addr = mmap(NULL, length, PROT_READ | PROT_WRITE, mode, fd, 0);
	if (addr == MAP_FAILED) {
		free(mem);
		return NULL;
	}
	
	mem->fd = fd;
	mem->file_size = 0;
	mem->length = length;
	mem->base_addr = addr;
	mem->pagesize = pagesize;
	mem->size = sizeof(struct cm_info);
	mem->bitmap = calloc(length / 8  / 64, sizeof(uint64_t));
	if (!mem->bitmap) {
		munmap(addr, length);
		free(mem), mem = NULL;
	}
	
	return mem;
}

int cm_delete(struct cm_attr *mem)
{
	int rc;      
 
	if (!mem) {
		errno = EINVAL;
		return -1;
	}	
	
	rc = munmap(mem->base_addr, mem->length);
	free(mem->bitmap);
	free(mem);
	return rc;
}

ssize_t cm_get_size(struct cm_attr *mem)
{
	return (mem) ? (ssize_t)mem->size : -1;
}

ssize_t cm_get_free_size(struct cm_attr *mem)
{
	return (mem) ? (ssize_t)(mem->length - mem->size) : -1;
}

void *cm_default_alloc(struct cm_attr *mem, size_t size)
{
	void *ptr;
	size_t offset;
	const size_t align = sizeof(void *);
		
	if (unlikely(!mem)) {
		errno = EINVAL;
		return NULL;	
	}

	if ((size_t)cm_get_free_size(mem) - align <= size) {
		errno = ENOMEM;
		return NULL;
	}

	ptr = mem->base_addr + mem->size;
	ptr = (void *)ALIGN((uintptr_t)ptr, align);
	offset = ptr - (mem->base_addr + mem->size);
	size += offset;

	if (unlikely(cm_grow(mem, mem->size + size)))
		return NULL;

	mem->size += size;
	return ptr;
}

void cm_default_free(struct cm_attr *mem UNUSED, void *ptr UNUSED)
{
	/* nothing to do here */
}

static void *cm_append(struct cm_attr *mem, struct iovec iov)
{
	void *p = cm_default_alloc(mem, iov.iov_len);

	if (!p)
		return NULL;

	memcpy(p, iov.iov_base, iov.iov_len);
	return p;
}

int cm_add_ptr(struct cm_attr *mem, void *addr) 
{
	if (unlikely(!mem || !addr)) {
		errno = EINVAL;
		return -1;
	}
	
	bm_addr_set(mem->base_addr, addr, mem->bitmap);	
	return 0;
}

static void cm_unset_null_ptr(struct cm_attr *mem)
{
	uintptr_t ptr;
	uint64_t *bm = mem->bitmap;
	void *addr = mem->base_addr;
	uintptr_t ptr_lbound = (uintptr_t)mem->base_addr;
	uintptr_t ptr_ubound = (uintptr_t)(mem->base_addr + mem->size);

	#pragma omp parallel for schedule(static)
	for (ptr = ptr_lbound; ptr < ptr_ubound; ptr += sizeof(void *)) 
		if (bm_addr_isset(addr, ptr, bm) && !(*(uintptr_t *)ptr))
			bm_addr_unset(addr, ptr, bm);
}

/* XXX: return iovec? */
void *cm_serialize(struct cm_attr *mem, void *root, int flags)
{
	void *bm;
	struct iovec iov;
	uintptr_t ptr_lbound, ptr_ubound;
	const size_t align = sizeof(void *);
	const size_t elem_size = sizeof(uint64_t);
	struct cm_info *info = mem ? mem->base_addr : NULL;

	if (!mem || !root) {
		errno = EINVAL;
		return NULL;
	}

	if (!(flags & CM_NO_NULL))
		cm_unset_null_ptr(mem);	

	ptr_lbound = (uintptr_t)mem->base_addr;
	ptr_ubound = (uintptr_t)(mem->base_addr + mem->size);
	if (ptr_ubound & (align - 1))
		ptr_ubound = ALIGN(ptr_ubound - align, align); 
	iov.iov_len = (((ptr_ubound - ptr_lbound) >> 9) + 1) * elem_size;
	iov.iov_base = mem->bitmap;
	bm = cm_append(mem, iov);
	if (!bm)
		return NULL;
	
	if (cm_truncate(mem, mem->size))
		return NULL;

	info->bm_offset = bm - mem->base_addr;
	info->bm_length = iov.iov_len;
	info->root = (uintptr_t)root;
	info->base_addr = (uintptr_t)mem->base_addr;	
	return mem->base_addr;
}

static void cm_des_k0(void *addr, size_t len, struct cm_info *info, int64_t diff)
{
	uintptr_t ptr;
	uint64_t *bm = addr + info->bm_offset;
	uintptr_t ptr_lbound = (uintptr_t)addr;
	uintptr_t ptr_ubound = (uintptr_t)(addr + len - info->bm_length);

	#pragma omp parallel for schedule(static)
	for (ptr = ptr_lbound; ptr < ptr_ubound; ptr += sizeof(void *)) 
		*(uintptr_t *)ptr += bm_addr_isset(addr, ptr, bm) ? diff : 0;		
}

/* TODO: vectorize */
static void cm_des_k1(void *addr, size_t len, struct cm_info *info, int64_t diff)
{
	uintptr_t p; 
	uint64_t v, c;
	size_t i, j, n; 
	const size_t align = sizeof(void *);
	uint64_t *bm = addr + info->bm_offset;
	uintptr_t ptr_lbound = (uintptr_t)addr;
	uintptr_t ptr_ubound = (uintptr_t)(addr + len - info->bm_length);

	if (ptr_ubound & (align - 1))
		ptr_ubound = ALIGN(ptr_ubound - align, align); 
	n = ((ptr_ubound - ptr_lbound) >> 9) - 1;
	#pragma omp parallel
	{
	#pragma omp for private(j, p, v, c) schedule(static) nowait
	for (i = 0; i < n; i++) {
		v = bm[i];
		p = ptr_lbound + (i << 9); 
		#pragma GCC unroll 64
		for (j = 0; j < 64; j++) {
			c = !!(v & (1ull << j));
			*(uintptr_t *)p += diff * c;
			p += sizeof(void *);
		}
	}
	#pragma omp single
	for (p = ptr_lbound + (n << 9); p < ptr_ubound; p += sizeof(void *))
		*(uintptr_t *)p += bitmap_isset(ptr_lbound, p, bm) ? diff : 0;
	}
}

void *cm_deserialize(void *addr, const size_t len)
{
	int oor;
	int64_t diff;
	struct cm_info *info = addr ? addr : NULL;

	if (!addr) {
		errno = EINVAL;
		return NULL;
	}

	oor = subptr_64((uintptr_t)addr, info->base_addr, &diff);
	if (oor) {
		errno = ERANGE;
		return NULL;
	}

	if (!diff) 
		return (void *)info->root;

	cm_des_k0(addr, len, info, diff);
	return (void *)(info->root + diff);
}

