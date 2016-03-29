#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>

#include "cma.h"

static int cma_init = 0;
static struct memory mem = { 
	.fd = -1, 
	.mode = MAP_ANON | MAP_SHARED,
	.map_size = 0,
	.vlq_size = 0,
	.vlq_sum = 0,
	.addr_list = NULL,
	.last_addr = NULL,
	.free_blocks = NULL
};

#define TsC(x)	(x = ((~x)+1))

static int64_t reverse_VLQ(int64_t vlq)
{
	size_t size = sizeof(int64_t);
	int i;
	union {
		char t[size];
		int64_t v;
	} old, new;
       
	for (old.v = vlq, new.v = 0, i = 0; i < size; i++) 
		new.t[size - i - 1] = old.t[i];
	
	return new.v;
}

/* Read a reverse VLQ (signed and unsigned) */
static size_t read_VLQ(int32_t *vlq, const int8_t *c) 
{
	int32_t x = 0;
	size_t n = 0;

	do {
		x = (x << 6) | (int32_t)(*c & 0x3F);
		n++;
	} while (*c-- & 0x80);

	if (*(c + 1) & 0x40)
		*vlq = TsC(x);
	else
		*vlq = x;

	return n;
}

/*  
 *  A negative value can be represented
 *  Return the number of bytes used for the vlq 
 */
static size_t write_VLQ(int64_t *vlq, int32_t offset)
{	
	size_t n;
	*vlq = 0;

	if (offset < 0) {
		TsC(offset);
		*vlq |= 0x40;
	}

	*vlq |= (int64_t)offset & 0x3F;
	
	for (n = 1; (offset >>= 6); n++) {
		*vlq <<= 8;
		*vlq |= ((offset & 0x3F) | 0x80);
	}	

	return n;	
}

static void **recalculate_addr(void *old_addr, void *new_addr, void **allocating)
{
	struct address_list *al = mem.addr_list;
	ptrdiff_t translation_coeff = new_addr - old_addr; /* should be negative */

	mem.base_addr = new_addr;
	*mem.root += translation_coeff;
	allocating += (translation_coeff / sizeof(allocating));

	while (al) {
		al->ptr += translation_coeff;
		al->addr += (translation_coeff / sizeof(al->addr));
		*al->addr = al->ptr;
		al = al->next;
	}

	return allocating;
}

static int cm_grow(void **addr, void *ptr, size_t len)
{	
	struct address_list *new_addr;

	new_addr = malloc(sizeof(*new_addr));
	if (!new_addr) 
		return 1;
	
	new_addr->ptr = ptr;
	new_addr->addr = addr;
	new_addr->len = len;
	new_addr->next = NULL;
	
	/* offset can be negative */	
	new_addr->offset = ((void *)addr - mem.base_addr) - mem.vlq_sum;
	mem.vlq_size += write_VLQ(&new_addr->vlq, new_addr->offset);
	mem.vlq_sum += new_addr->offset;

	if (!mem.addr_list) 
		mem.addr_list = new_addr;
	else
		mem.last_addr->next = new_addr;

	mem.last_addr = new_addr;	
	return 0;
}

size_t cm_get_size(void)
{
	return mem.map_size;
}

size_t cm_get_pre_size(void) /* add static verification */
{
	return mem.map_size + mem.vlq_size;
}

void cm_set_properties(int fd, mode_t mode)
{	
	if (!cma_init) {
		mem.fd = fd;
		mem.mode = mode;
	}
}

/* add protection after cm_sync() (actualy create fragmentation) */
int cm_allocator(void **addr, size_t size, int flag)
{
	void *ptr;
	void *old_addr;

	if (mem.fd != -1) { 
		if (ftruncate(mem.fd, mem.map_size + size) == -1) {
			*addr = NULL;
			return -1;
		}	
	}

	if (!cma_init) { 
		ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, mem.mode, mem.fd, 0);
		if (ptr == MAP_FAILED)
			goto err;
		mem.base_addr = ptr;
		mem.root = addr;
		cma_init = 1;
	} else {
		old_addr = mem.base_addr;
		ptr = mremap(old_addr, mem.map_size, mem.map_size + size,
			     MREMAP_MAYMOVE);
		if (ptr == MAP_FAILED)
			goto err;
		if (ptr != old_addr) 
			addr = recalculate_addr(old_addr, ptr, addr);
		ptr += mem.map_size;
		if (flag && cm_grow(addr, ptr, size) != 0)
			goto err;
	}

	*addr = ptr;	
	mem.map_size += size;
	return 0;
err:
	if (mem.fd != -1) 
		ftruncate(mem.fd, mem.map_size);
	*addr = NULL;
	return -1;	
}

int affect_ptr(void **ptr, void *to)
{
	if (cma_init) {
		*ptr = to;
		return cm_grow(ptr, to, 0);	
	} 

	return 1;
}


void *cm_sync(int flags)
{
	size_t size, indice = mem.vlq_size;
	int8_t *vlq;
	struct address_list *al = mem.addr_list;

	if (!cma_init || cm_allocator((void **)&vlq, mem.vlq_size + 1, 0))
		return NULL;

	while (al) {
		size = sizeof(int64_t) - 1;
		do {
			al->vlq = reverse_VLQ(al->vlq);
			vlq[indice--] = al->vlq_c[size];	
		} while (al->vlq_c[size--] & 0x80);
		al = al->next;
	}	
	
	vlq[indice] = 0;
	if (mem.fd != -1 && msync(mem.base_addr, mem.map_size, flags) == -1)
		return NULL;

	return mem.base_addr;
}

void cm_processing_r(void **addr, size_t object_size, size_t data_size)
{
	int32_t offset;
	ptrdiff_t translation_coeff; 
	uintptr_t old_base_addr;
	int8_t *vlq = *addr + (data_size - 1);
	void *start_addr = *addr;

	vlq -= read_VLQ(&offset, vlq);
	old_base_addr = *((uintptr_t *)(*addr + offset)) - object_size;
	translation_coeff = (uintptr_t)*addr - old_base_addr; 

	/* do not processing if the old and new addr are the same */
	if (!translation_coeff) { 
		return;
	} else {
		*addr += offset;
		*((uintptr_t *)*addr) += translation_coeff;
	}
	
	for (vlq -= read_VLQ(&offset, vlq); offset; 
	     vlq -= read_VLQ(&offset, vlq)) {
		*addr += offset;
		*((uintptr_t *)*addr) += translation_coeff;
	}

	translation_coeff = *addr - start_addr;
	*addr -= translation_coeff;
}

int cm_free(void)
{
	int ret;
	struct address_list *al;
	struct free_block *fb;

	ret = munmap(mem.base_addr, mem.map_size);
	mem.base_addr = NULL;
	mem.map_size = 0;
	mem.vlq_size = 0;
	mem.vlq_sum = 0;

	while ((al = mem.addr_list) != NULL) {
		mem.addr_list = al->next;
		free(al), al = NULL;
	}

	while ((fb = mem.free_blocks) != NULL) {
		mem.free_blocks = fb->next;
		free(fb), fb = NULL;
	}

	mem.fd = -1;
	mem.mode = MAP_ANON | MAP_SHARED;
	mem.last_addr = NULL;
	cma_init = 0;	
	return ret;
}

