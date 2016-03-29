#ifndef _CMA_H
#define _CMA_H

#define GROW		0x01
#define FIRST_FIT	0x02 /* not used yet */

struct memory {
	int fd;
	mode_t mode;
	size_t map_size;
	size_t vlq_size;
	int32_t vlq_sum;
	void *base_addr;
	void **root;
	struct address_list *addr_list;
	struct address_list *last_addr;
	struct free_block *free_blocks;
};

struct address_list {
	void *ptr;
	void **addr;
	size_t len;
	int32_t offset;
	union { 
		int8_t vlq_c[sizeof(int64_t)];
		int64_t vlq;
	};
	struct address_list *next;
};

struct free_block { /* not used yet */
	void *addr;
	size_t len;
	struct free_block *next;
};

/* 
 * Allocate Contiguous Memory (CM), it is used as malloc. You should 
 * use cma() macro instead of cm_allocator().  
 */
#define cma(ADDR, X)	cm_allocator((void **)ADDR, X, GROW)
int cm_allocator(void **addr, size_t size, int flag);

int cm_realloc_block(void **addr, size_t new_size); /* not used yet */
int cm_free_block(void *addr); /* not used yet */

/* 
 * Used for posix shared memory and file mapped. 
 * Call it before the first cma()
 */
void cm_set_properties(int fd, mode_t mode);

/* affect a pointer in CM from CM */
#define ptr_to(P, X)	affect_ptr((void **)P, X)
int affect_ptr(void **ptr, void *to);

/* 
 * Synchronize the CM. Return a ptr at the start of CM or NULL on error. 
 * Flags are used for msync() (only for posix shm and mapped file)
 */
void *cm_sync(int flags); 

/* return the actual size of CM */
size_t cm_get_size(void);

/*
 * Return the size of the CM should have after a cm_sync() 
 * Useful for fct like send(): send(fd, cm_sync(0), cm_get_pre_size(), 0);
 */
size_t cm_get_pre_size(void);

/* free the CM and reset his properties */
int cm_free(void);

/* 
 * Used on client application just after getting the buffer (addr).
 * The memory containing the buffer have to be writeable (in shm and 
 * mapped file)
 */
#define cm_processing(X, O_SIZE, D_SIZE) cm_processing_r((void **)X, O_SIZE, D_SIZE)
void cm_processing_r(void **addr, size_t object_size, size_t data_size);

#endif
