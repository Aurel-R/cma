#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
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
 * (U)LONG_MAX from limits.h ? */ 
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

static inline int64_t reverse_vlq(int64_t vlq)
{
	return  ((vlq << 56) & 0xff00000000000000UL) |
		((vlq << 40) & 0x00ff000000000000UL) |
		((vlq << 24) & 0x0000ff0000000000UL) |
		((vlq <<  8) & 0x000000ff00000000UL) |
		((vlq >>  8) & 0x00000000ff000000UL) |
		((vlq >> 24) & 0x0000000000ff0000UL) |
		((vlq >> 40) & 0x000000000000ff00UL) |
	       	((vlq >> 56) & 0x00000000000000ffUL);
}

static inline size_t read_vlq(int32_t *restrict offset, 
					const int8_t *restrict vlq) 
{
	size_t n;
	int32_t x = (int32_t)(*vlq & 0x3f);
	
	for (n = 1; *vlq-- & 0x80; n++) 
		x = (x << 6) | (int32_t)(*vlq & 0x3f);
	
	*offset = (*(vlq + 1) & 0x40) ? ~x + 1 : x;
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
	
	if (vlq_sum >= 0)
		return !(sub <= offset && sub != INT32_MIN);
	
	return !(sub >= offset);
}

/* XXX: More clarification are needed
 * Behavior depends on the pointers provenance. In all cases, 
 * pointer to integer casts (and vice verca) depends on the 
 * implementation (implementation defined for (u)intptr_t types).
 * Implementation to/from unsigned long for LP64 has the same behavior ?
 * In case:
 *     To the same object(+1) or to members of the same object 
 *     (array, struct, union) the behavior is defined. It should
 *     be noted that (pointer) arithmetic on uintptr_t is unspecified
 *     (probably implementation defined). It conerns cm_grow() function.
 * else (recalculate_addr() and cm_do_deserialize())
 *     C11: undefined behaviour (probably implementation defined, 
 *	    does not clearly specify)
 *     C2X: yes with no-provenance option proposal
 */
static inline int64_t subptr_64(const void *const ptr1, 
					const void *const ptr2, int *overflow)
{
	const uintptr_t _ptr1 = (uintptr_t)ptr1;
	const uintptr_t _ptr2 = (uintptr_t)ptr2;
	uint64_t diff = MAX(_ptr1, _ptr2) - MIN(_ptr1, _ptr2);

	if (diff <= (uint64_t)INT64_MAX) 
		return (_ptr1 < _ptr2) ? (int64_t)(~diff + 1) : (int64_t)diff;

	*overflow = 1;
	return -1;
}

static inline int32_t subptr_32(const void *const ptr1, 
					const void *const ptr2, int *overflow) 
{
	int64_t diff_64 = subptr_64(ptr1, ptr2, overflow);
	
	if (!*overflow && diff_64 <= INT32_MAX && diff_64 >= INT32_MIN)
		return (int32_t)diff_64;
	
	*overflow = 1;
	return -1;	
}

static inline int32_t get_ptrdiff(const void *const ptr1, 
					const void *const ptr2, int *overflow)
{
	*overflow = 0;
	return subptr_32(ptr1, ptr2, overflow);	
}

struct cm_attr *cm_create(int fd, size_t length, int mode, int flags)
{
	void *addr;
	struct cm_attr *mem;
	
	addr = mmap(NULL, length, PROT_READ | PROT_WRITE, mode, fd, 0);
	if (unlikely(addr == MAP_FAILED))
		return NULL;

	mem = malloc(sizeof(*mem));
	if (unlikely(!mem)) {
		munmap(addr, length);
		return NULL;
	}

	mem->fd = fd;
	mem->mode = mode;
	mem->flags = flags;
	mem->length = length;
	mem->base_addr = addr;
	mem->root = NULL;
	mem->map_size = 0;
	mem->vlq_size = 0;
	mem->vlq_sum = 0;
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

	free(mem);
	return retval;
}

static int recalculate_addr(struct cm_attr *mem, 
				void *restrict const old_addr, 
				void *restrict const new_addr)
{
	struct cm_addr_list *al;	
	int64_t translation_coeff;
	int overflow = 0;

	mem->base_addr = new_addr;
	translation_coeff = subptr_64(new_addr, old_addr, &overflow);
	if (overflow) {
		errno = ERANGE;
		return -1;
	}

	/* XXX: Is it better to do
	 *	*mem->root = new_addr + (*mem->root - old_addr);  
	 * instead of calculation of translation coefficient ?
	 * ex see: 
	 * issues.apache.org/jira/secure/attachment/12488206/0_7_0_ptrdiff.patch
	 * Probably not due to compiler optimization (possibly same end code).
	 */
	if (mem->root)
		*mem->root += translation_coeff;

	for (al = mem->addr_list; al; al = al->next) {
		if (al->ptr)
			al->ptr += translation_coeff; 
		al->addr += translation_coeff;
		*al->addr = al->ptr;
	}

	return 0;	
}

static int cm_extend(struct cm_attr *mem, size_t size, int flags)
{
	void *new_addr;
	void *old_addr;
       
	if (!mem) {
		errno = EFAULT;
		return -1;
	}	

	old_addr = mem->base_addr;
	new_addr = mremap(old_addr, mem->length, mem->length + size, flags);
	if (new_addr == MAP_FAILED)
		return -1;
	
	if (new_addr != old_addr && recalculate_addr(mem, old_addr, new_addr))
		return -1;

	mem->length += size;
	return 0;
}

int cm_try_extend(struct cm_attr *mem, size_t size)
{
	return cm_extend(mem, size, 0);
}

int cm_force_extend(struct cm_attr *mem, size_t size)
{
	return cm_extend(mem, size, MREMAP_MAYMOVE);
}

ssize_t cm_get_size(struct cm_attr *mem)
{
	return (mem) ? (ssize_t)mem->map_size : -1;
}

ssize_t cm_get_pre_size(struct cm_attr *mem)
{
	return (mem) ? (ssize_t)(mem->map_size + mem->vlq_size) : -1;
}

ssize_t cm_get_free_size(struct cm_attr *mem)
{
	return (mem) ? (ssize_t)(mem->length - 
				(mem->map_size + mem->vlq_size)) : -1;
}

/* 
 * TODO: add test for 
 * - vlq_size (can wrap)
 * - vlq_sum (can overflow/underflow)
 */
static int cm_grow(struct cm_attr *mem, void **addr, void *ptr, size_t len)
{	
	int overflow;
	struct cm_addr_list *new_addr;

	new_addr = malloc(sizeof(*new_addr));
	if (unlikely(!new_addr)) 
		return -1;

	/* XXX: ptr can be null */	
	new_addr->ptr = ptr; 
	new_addr->addr = addr;
	new_addr->len = len;
	new_addr->next = NULL;
	new_addr->offset = get_ptrdiff(addr, mem->base_addr, &overflow);
	
	if (overflow || oor_vlq(new_addr->offset, mem->vlq_sum)) {
		free(new_addr);
		errno = ERANGE;
		return -1;
	}

	new_addr->offset -= mem->vlq_sum;
	mem->vlq_size += write_vlq(&new_addr->vlq, new_addr->offset); 
	mem->vlq_sum += new_addr->offset; 

	if (!mem->addr_list) 
		mem->addr_list = new_addr;
	else
		mem->last_addr->next = new_addr;

	mem->last_addr = new_addr;	
	return 0;
}

static inline int trunc_file(int fd, size_t size, int flag)
{
	if (!(flag & UNTRUNCABLE_FILE) && fd != -1)
		return ftruncate(fd, size);

	return 0;
}

int cm_do_alloc(struct cm_attr *mem, void **addr, size_t size, int flag)
{
	int serrno;
	void *ptr;
		
	if (!mem || !addr) {
		errno = EFAULT;
		return -1;	
	}

	if ((size_t)cm_get_free_size(mem) <= size) {
		errno = ENOMEM;
		return -1;
	}

	if (trunc_file(mem->fd, mem->map_size + size, mem->flags))
		return -1;

	ptr = mem->base_addr + mem->map_size;
	if (mem->root && flag && cm_grow(mem, addr, ptr, size))
		goto err;

	if (!mem->root)
		mem->root = addr;

	*addr = ptr;
	mem->map_size += size;
	return 0;
err:
	serrno = errno;
	trunc_file(mem->fd, mem->map_size, mem->flags);
	errno = serrno;
	return -1;	
}

int cm_affect_ptr(struct cm_attr *mem, void **ptr, void *to)
{
	struct cm_addr_list *al;

	if (!mem || !ptr) {   
		errno = EFAULT;
		return -1;
	}

	*ptr = to;
	for (al = mem->addr_list; al; al = al->next) {
		if (al->addr == ptr) {
			al->ptr = to;
			return 0;
		}
	}

	return cm_grow(mem, ptr, to, 0);	
}

void *cm_serialize(struct cm_attr *mem, int flags)
{
	int8_t *vlq;
	size_t size, index;
	struct cm_addr_list *al;

	if (!mem) {
		errno = EFAULT;
		return NULL;
	}

	index = mem->vlq_size;
	if (cm_do_alloc(mem, (void **)&vlq, index + 1, 0))
		return NULL;

	for (al = mem->addr_list; al; al = al->next) {
		size = sizeof(int64_t) - 1;
		do {
			/* XXX: multiple reverse on vlq value if
			 * serialize is called twice */
			al->vlq = reverse_vlq(al->vlq);
			vlq[index--] = al->vlq_c[size];
		} while (al->vlq_c[size--] & 0x80);
	}

	vlq[index] = 0;
	if (flags && mem->fd != -1 && !(mem->flags & UNTRUNCABLE_FILE) &&
	    msync(mem->base_addr, mem->map_size, flags) == -1)
		return NULL;

	return mem->base_addr;
}

int cm_do_deserialize(void **addr, size_t object_size, size_t data_size)
{
	int32_t offset;
	uintptr_t old_base_addr;
	int64_t translation_coeff;
	int8_t *vlq = *addr + (data_size - 1);
	void *start_addr = *addr;
	int overflow = 0;

	vlq -= read_vlq(&offset, vlq);
	old_base_addr = *((uintptr_t *)(*addr + offset)) - object_size;
	translation_coeff = subptr_64(*addr, (void *)old_base_addr, &overflow);
	if (overflow) {
		errno = ERANGE;
		return -1;
	}	

	if (!translation_coeff)
		return 0;
	
	*addr += offset;
	*((uintptr_t *)*addr) +=  (void *)*((uintptr_t *)*addr) ? 
							translation_coeff : 0;
	for (vlq -= read_vlq(&offset, vlq); offset; 
	     vlq -= read_vlq(&offset, vlq)) {
		*addr += offset;
		*((uintptr_t *)*addr) +=  (void *)*((uintptr_t *)*addr) ? 
							translation_coeff : 0;
	}

	*addr = start_addr;
	return 0;
}

