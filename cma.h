#ifndef CMA_H
#define CMA_H

struct cm_attr {
	int fd;
	size_t length;
	size_t size;
	size_t file_size;
	size_t pagesize;
	void *base_addr;
	uint64_t *bitmap;
};

struct cm_attr *cm_create(int fd, size_t length, int mode);
int cm_delete(struct cm_attr *mem);

ssize_t cm_get_size(struct cm_attr *mem);
ssize_t cm_get_free_size(struct cm_attr *mem);

void *cm_default_alloc(struct cm_attr *mem, size_t size);
void cm_default_free(struct cm_attr *mem, void *ptr);

int cm_add_ptr(struct cm_attr *mem, void *addr);

#define CM_NO_NULL	0x01
void *cm_serialize(struct cm_attr *mem, void *root, int flags);
void *cm_deserialize(void *addr, const size_t len);

#endif

