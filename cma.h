#ifndef _CMA_H
#define _CMA_H

struct cm_attr {
	int fd;
	int mode;
	int flags;
	size_t length;
	void *base_addr;
	void **root;
	size_t map_size;
	size_t vlq_size;
	int32_t vlq_sum;
	struct cm_addr_list *addr_list;
	struct cm_addr_list *last_addr;
};

struct cm_addr_list {
	void *ptr;
	void **addr;
	int32_t offset;
	size_t len;
	union { 
		int8_t vlq_c[sizeof(int64_t)];
		int64_t vlq;
	};
	struct cm_addr_list *next;
};

#define UNTRUNCABLE_FILE	0x01
struct cm_attr *cm_create(int fd, size_t length, int mode, int flags);
#define CM_EREASE	0x00
#define CM_PRESERVE	0x01
ssize_t cm_delete(struct cm_attr *mem, int flag);

int cm_try_extend(struct cm_attr *mem, size_t size);
int cm_force_extend(struct cm_attr *mem, size_t size);

ssize_t cm_get_size(struct cm_attr *mem);
ssize_t cm_get_pre_size(struct cm_attr *mem);
ssize_t cm_get_free_size(struct cm_attr *mem);

#define CM_GROW	0x01 
#define cm_alloc(CM, ADDR, SIZE) cm_do_alloc(CM, (void **)ADDR, SIZE, CM_GROW)
int cm_do_alloc(struct cm_attr *mem, void **addr, size_t size, int flag);
#define cm_ptr_to(CM, PTR, TO) cm_affect_ptr(CM, (void **)PTR, TO) 
int cm_affect_ptr(struct cm_attr *mem, void **ptr, void *to);

void *cm_serialize(struct cm_attr *mem, int flags);
#define cm_deserialize(ADDR, O_SIZE, D_SIZE) \
		cm_do_deserialize((void **)ADDR, O_SIZE, D_SIZE)
int cm_do_deserialize(void **addr, size_t object_size, size_t data_size);

#endif
