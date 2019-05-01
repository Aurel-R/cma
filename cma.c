#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "cma.h"

/* Technically, big-endian could be supported if reverse_vlq() is not called */ 
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "little-endian is required."
#endif
#if !defined(__LP64__) && !defined(_LP64)
#error "LP64 is required."
#endif
/* It can't normally happen, unless (u)intptr_t is not defined (optional types). 
 * In case, redefined (u)intptr_t from (unsigned) long (LP64 system) and use
 * (U)LONG_MAX from limits.h */ 
#if INTPTR_MAX != INT64_MAX || UINTPTR_MAX != UINT64_MAX
#error "incompatible types (u)intptr_t (u)int64_t."
#endif

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

#define ALIGN(x, align)	(((x) + (align) - 1) & ~((align) - 1))

#define CM_OBA_OFFSET(X, S)	(X + S - sizeof(void *))
#define CM_NPTR_OFFSET(X, S)	(CM_OBA_OFFSET(X, S)  - sizeof(size_t))
#define CM_VLQ_OFFSET(X, S)	(CM_NPTR_OFFSET(X, S) - 1)

static inline int64_t reverse_vlq(const int64_t vlq)
{
	return  ((vlq << 56) & 0xff00000000000000ul) |
		((vlq << 40) & 0x00ff000000000000ul) |
		((vlq << 24) & 0x0000ff0000000000ul) |
		((vlq <<  8) & 0x000000ff00000000ul) |
		((vlq >>  8) & 0x00000000ff000000ul) |
		((vlq >> 24) & 0x0000000000ff0000ul) |
		((vlq >> 40) & 0x000000000000ff00ul) |
	       	((vlq >> 56) & 0x00000000000000fful);
}

static inline size_t read_vlq(int32_t *restrict offset,
					const int8_t *restrict vlq)
{
	size_t n;
	*offset = (int32_t)(*vlq & 0x3f);

	for (n = 1; (*vlq-- & 0x80) && n < 6; n++)
		*offset = (*offset << 6) | (int32_t)(*vlq & 0x3f);
	
	*offset = (*(vlq + 1) & 0x40) ? ~(*offset) + 1 : *offset;
	return n;
}

static size_t write_vlq(int64_t *vlq, int32_t offset)
{	
	size_t n;
	*vlq = 0;
	
	if (offset < 0) {
		offset = ~offset + 1;
		*vlq |= 0x40;
	}
	
	*vlq |= (int64_t)offset & 0x3f;

	for (n = 1; offset >>= 6; n++) 
		*vlq = (*vlq << 8) | ((offset & 0x3f) | 0x80);

	return n;	
}

static inline int oor_vlq(const int32_t offset, const int32_t vlq_sum)
{
	const int32_t sub = offset - vlq_sum;

	return vlq_sum  >= 0
		? !(sub <= offset && sub != INT32_MIN)
		: !(sub >= offset);
}

static inline int64_t subptr_64(const uintptr_t p, const uintptr_t q, int *oor) 
{
	const uint64_t diff = MAX(p, q) - MIN(p, q);

	if (diff <= (uint64_t)INT64_MAX) {
		*oor = 0;
		return (p < q) ? ~diff + 1 : diff;
	}

	*oor = 1;
	return -1;
}

static inline int32_t subptr_32(const uintptr_t p, const uintptr_t q, int *oor) 
{
	const int64_t diff_64 = subptr_64(p, q, oor);
	
	if (!*oor && diff_64 <= INT32_MAX && diff_64 >= INT32_MIN)
		return diff_64;
	
	*oor = 1;
	return -1;	
}

static inline int32_t get_ptrdiff(const void *const p, 
					const void *const q, int *oor)
{
	return subptr_32((uintptr_t)p, (uintptr_t)q, oor);
}

/* TODO: grow by chunk (PAGE_SIZE) */
static inline int trunc_file(int fd, size_t size, int flags)
{
	return (fd == -1 || flags & UNTRUNCABLE_FILE) 
		? 0 
		: ftruncate(fd, size);
}

struct cm_attr *cm_create(int fd, size_t length, int mode, int flags)
{
	void *addr;
	struct cm_attr *mem;
	const  size_t addr_align = sizeof(void *);
	const ssize_t page_align = (ssize_t)sysconf(_SC_PAGESIZE);

	if (unlikely(page_align < 0)) 
		return NULL;

	length = ALIGN(length, (size_t)page_align);
	addr = mmap(NULL, length, PROT_READ | PROT_WRITE, mode, fd, 0);
	if (unlikely(addr == MAP_FAILED))
		return NULL;

	if (unlikely(trunc_file(fd, sizeof(void *), flags))) {
		munmap(addr, length);
		return NULL;
	}

	mem = malloc(sizeof(*mem));
	if (unlikely(!mem)) {
		munmap(addr, length);
		return NULL;
	}
	
	/* TODO: 
	 *	- use mmap + call before 1st mmap() 
	 *	- addr_map size can be divided by two if length < 4GB */
	mem->addr_map = calloc(length / addr_align, sizeof(void *));
	if (unlikely(!mem->addr_map)) {
		munmap(addr, length);
		free(mem);
		return NULL;
	}
	
	mem->fd = fd;
	mem->flags = flags;
	mem->pagesize = (size_t)page_align;
	mem->length = length;
	mem->base_addr = addr;
	mem->map_size = sizeof(void *);
	mem->vlq_size = 0;
	mem->vlq_sum = 0;
	mem->nb_of_ptr = 0;
	mem->addr_list = NULL;
	mem->last_addr = NULL;
	return mem;
}

ssize_t cm_delete(struct cm_attr *mem, int flag)
{
	struct cm_addr_list *al;
	ssize_t retval;
       
	if (!mem) {
		errno = EFAULT;
		return -1;
	}	
	
	retval = mem->length;
	if (flag == CM_EREASE)
		retval = munmap(mem->base_addr, mem->length);

	while ((al = mem->addr_list) != NULL) {
		mem->addr_list = al->next;
		free(al), al = NULL;
	}

	free(mem->addr_map);
	free(mem);
	return retval;
}

/* XXX: deprecated */
static int recalculate_addr(struct cm_attr *mem, 
				void *const old_addr, 
				void *const new_addr)
{
	int oor;
	uintptr_t ptr;
	int64_t shift_value;
	struct cm_addr_list *al;	
	const uintptr_t _old_addr = (uintptr_t)old_addr;
	const uintptr_t _new_addr = (uintptr_t)new_addr;

	mem->base_addr = new_addr;
	shift_value = subptr_64(_new_addr, _old_addr, &oor);
	if (oor) {
		errno = ERANGE;
		return -1;
	}

	for (al = mem->addr_list; al; al = al->next) {
		al->addr += shift_value;
		ptr = (uintptr_t)*al->addr;
		ptr += ptr ? shift_value : 0;
		*al->addr = (void *)ptr;
	}

	return 0;	
}

/* XXX: deprecated */
static int cm_extend(struct cm_attr *mem, size_t size, int flags)
{
	void *tmp;
	void *new_addr;
	void *old_addr; /* XXX: it seem better to use uintptr_t */
	size_t new_len;

	if (!mem) {
		errno = EFAULT;
		return -1;
	}	

	new_len = mem->length + size;
	new_len = ALIGN(new_len, mem->pagesize);
	old_addr = mem->base_addr;
	new_addr = mremap(old_addr, mem->length, new_len, flags);
	if (new_addr == MAP_FAILED)
		return -1;

	tmp = realloc(mem->addr_map, new_len);
	if (unlikely(!tmp)) {
		old_addr = new_addr;
		new_addr = mremap(old_addr, new_len, mem->length, 0);
		return -1;
	}
	mem->addr_map = tmp;
	memset(mem->addr_map + mem->length, 0, new_len - mem->length); 

	/* XXX: old_addr is undeterminate (value and representation) 
	 *	comparison could be undefined behavior (even if mremap
	 *	does not move the mapping) */
	if (new_addr != old_addr && recalculate_addr(mem, old_addr, new_addr))
		return -1;

	mem->length = new_len;
	return 0;
}

/* XXX: deprecated */
int cm_try_extend(struct cm_attr *mem, size_t size)
{
	return cm_extend(mem, size, 0);
}

/* XXX: deprecated */
int cm_force_extend(struct cm_attr *mem, size_t size)
{
	return cm_extend(mem, size, MREMAP_MAYMOVE);
}

ssize_t cm_get_size(struct cm_attr *mem)
{
	return (mem) ? (ssize_t)mem->map_size : -1;
}

ssize_t cm_get_free_size(struct cm_attr *mem)
{
	return (mem) ? (ssize_t)(mem->length - 
				(mem->map_size + mem->vlq_size)) : -1;
}

/* TODO: add test for vlq_sum (can overflow/underflow) */
static int cm_grow(struct cm_attr *mem, void **addr)
{	
	int oor;
	int32_t offset;
	struct cm_addr_list *new_addr;
	size_t align = sizeof(void *);

	new_addr = malloc(sizeof(*new_addr));
	if (unlikely(!new_addr)) 
		return -1;

	new_addr->addr = addr;
	new_addr->next = NULL;
	offset = get_ptrdiff(addr, mem->base_addr, &oor);
	
	if (oor || oor_vlq(offset, mem->vlq_sum)) {
		free(new_addr);
		errno = ERANGE;
		return -1;
	}

	offset -= mem->vlq_sum;
	mem->vlq_size += write_vlq(&new_addr->vlq, offset); 
	mem->vlq_sum += offset; 

	if (!mem->addr_list) 
		mem->addr_list = new_addr;
	else
		mem->last_addr->next = new_addr;

	mem->last_addr = new_addr;
	mem->addr_map[((void *)addr - mem->base_addr) / align] = addr;
	mem->nb_of_ptr++;
	return 0;
}

static inline int in_range(struct cm_attr *mem, void **addr)
{
	return ((uintptr_t)addr >= (uintptr_t)mem->base_addr &&
		(uintptr_t)addr < (uintptr_t)(mem->base_addr + mem->length));
}

int cm_do_alloc(struct cm_attr *mem, void **addr, size_t size, int flags)
{
	void *ptr;
	int serrno;
	size_t offset;
	const size_t align = flags & CM_ALIGNED ? sizeof(void *) : 0;
		
	if (!mem || !addr) {
		errno = EFAULT;
		return -1;	
	}

	if ((size_t)cm_get_free_size(mem) - align <= size) {
		errno = ENOMEM;
		return -1;
	}

	ptr = mem->base_addr + mem->map_size;
	if (flags & CM_ALIGNED) {
		ptr = (void *)ALIGN((uintptr_t)ptr, align);
		offset = ptr - (mem->base_addr + mem->map_size);
		size += offset;
	}

	if (unlikely(trunc_file(mem->fd, mem->map_size + size, mem->flags)))
		return -1;

	if ((flags & CM_GROW) && in_range(mem, addr))
		if (unlikely(cm_grow(mem, addr)))
			goto err;

	*addr = ptr;
	mem->map_size += size;
	return 0;
err:
	serrno = errno;
	trunc_file(mem->fd, mem->map_size, mem->flags);
	errno = serrno;
	return -1;	
}

int cm_do_ptr_to(struct cm_attr *mem, void **ptr, void *to)
{
	void *addr;
	const size_t align = sizeof(void *);

	if (!mem || !ptr) { 
		errno = EFAULT;
		return -1;
	}
	
	if (!in_range(mem, ptr)) {
		errno = EINVAL;
		return -1;
	}

	*ptr = to;
	addr = mem->addr_map[((void *)ptr - mem->base_addr) / align];
	return (addr || !to) ? 0 : cm_grow(mem, ptr);
}

static void *cm_relative_serialize(struct cm_attr *mem, void *root, int flags)
{
	int8_t *vlq;
	size_t *nptr;
	void *base_addr;
	size_t size, index;
	struct cm_addr_list *al;

	index = mem->vlq_size;
	if (cm_do_alloc(mem, (void **)&vlq, index + 1, 0) 	||
	    cm_do_alloc(mem, (void **)&nptr, sizeof(size_t), 0) ||
	    cm_do_alloc(mem, &base_addr, sizeof(void *), 0))
		return NULL;
	
	for (al = mem->addr_list; al; al = al->next) {
		size = sizeof(int64_t) - 1;
		al->vlq = reverse_vlq(al->vlq);
		vlq[index--] = al->vlq_c[size];
		while (al->vlq_c[size--] & 0x80) 
			vlq[index--] = al->vlq_c[size];
		al->vlq = reverse_vlq(al->vlq);
	}	
	vlq[index] = 0;
	
	*nptr = 0;
	*(uintptr_t *)base_addr = (uintptr_t)mem->base_addr;
	*(uintptr_t *)mem->base_addr = (uintptr_t)root;
	if (flags && mem->fd != -1 && !(mem->flags & UNTRUNCABLE_FILE) &&
	    msync(mem->base_addr, mem->map_size, flags) == -1)
		return NULL;

	return mem->base_addr;
}

static void *cm_absolute_serialize(struct cm_attr *mem, void *root, int flags)
{
	size_t i, j;	
	size_t *nptr;
	void *base_addr;
	size_t *offset_table; /* XXX: if limited to 2GB, use int32_t */
	size_t n = mem->nb_of_ptr;
	void  *p = mem->base_addr;
	void **q = mem->addr_map;
	size_t m = mem->length / sizeof(void *);

	if (n & ~(-1 >> 3)) {
		errno = ERANGE;
		return NULL;
	}

	if (cm_do_alloc(mem, (void **)&offset_table, n << 3, CM_ALIGNED) ||
	    cm_do_alloc(mem, (void **)&nptr, sizeof(size_t), 0)		 ||
	    cm_do_alloc(mem, &base_addr, sizeof(void *), 0))
		return NULL;

	for (i = 0, j = 0; i < m && j < n; i++) {
		offset_table[j] = q[i] - p;
		j = q[i] ? j + 1 : j; 
	}

	*nptr = n;
	*(uintptr_t *)base_addr = (uintptr_t)mem->base_addr;
	*(uintptr_t *)mem->base_addr = (uintptr_t)root;
	if (flags && mem->fd != -1 && !(mem->flags & UNTRUNCABLE_FILE) &&
	    msync(mem->base_addr, mem->map_size, flags) == -1)
		return NULL;

	return mem->base_addr;
}

/* 
 * TODO: do not serialize null pointers:
 *	- remove ternary test in deserialize
 *	- smaller memory footprint 
 */
void *cm_serialize(struct cm_attr *mem, void *root,
				int serialize_flags, int msync_flags)
{
	if (!mem || !root) {
		errno = EFAULT;
		return NULL;
	}
	
	if (!in_range(mem, root))
		goto end;
	
	if (serialize_flags & CM_RELATIVE_OFFSET)
		return cm_relative_serialize(mem, root, msync_flags);
	if (serialize_flags & CM_ABSOLUTE_OFFSET)
		return cm_absolute_serialize(mem, root, msync_flags);
end:
	errno = EINVAL;
	return NULL;
}

static void cm_abs_deserialize(void *addr, const size_t *const nptr, 
						const int64_t shift_value)
{
	size_t i;
	const size_t *const offset_table = nptr - *nptr;

	#pragma omp parallel for schedule(static)
	for (i = 0; i < *nptr; i++) {
		*(uintptr_t *)(addr + offset_table[i]) += 
			*(uintptr_t *)(addr + offset_table[i])
						? shift_value : 0;
	}
}

static void cm_rel_deserialize(void *addr, int8_t *vlq, 
						const int64_t shift_value)
{
	int32_t offset;

	for (vlq -= read_vlq(&offset, vlq); offset; 
	     vlq -= read_vlq(&offset, vlq)) {
		addr += offset;
		*(uintptr_t *)addr += *(uintptr_t *)addr ? shift_value : 0;
	}
}

void *cm_deserialize(void *addr, const size_t len)
{
	int oor;
	int8_t *vlq = CM_VLQ_OFFSET(addr, len);
	const size_t *const nptr = CM_NPTR_OFFSET(addr, len);
	const uintptr_t old_addr = *(uintptr_t *)CM_OBA_OFFSET(addr, len);
	const int64_t shift_value = subptr_64((uintptr_t)addr, old_addr, &oor);

	if (oor) {
		errno = ERANGE;
		return NULL;
	}

	if (!shift_value) 
		return (void *)*(uintptr_t *)addr;

	if (*nptr)
		cm_abs_deserialize(addr, nptr, shift_value);
	else
		cm_rel_deserialize(addr, vlq,  shift_value);	

	*(uintptr_t *)addr += shift_value;	
	return (void *)*(uintptr_t *)addr;
}

