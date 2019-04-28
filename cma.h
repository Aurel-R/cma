#ifndef _CMA_H
#define _CMA_H

struct cm_attr {
	int fd;
	int flags;
	size_t pagesize;
	size_t length;
	void *base_addr;
	void **addr_map;
	size_t map_size;
	size_t vlq_size;
	int32_t vlq_sum;
	size_t nb_of_ptr;
	struct cm_addr_list *addr_list;
	struct cm_addr_list *last_addr;
};

struct cm_addr_list {
	void **addr;
	union { 
		int8_t vlq_c[sizeof(int64_t)];
		int64_t vlq;
	};
	struct cm_addr_list *next;
};

#define UNTRUNCABLE_FILE	0x01 /* /dev/zero for example */
struct cm_attr *cm_create(int fd, size_t length, int mode, int flags);
#define CM_EREASE	0x00
#define CM_PRESERVE	0x01
ssize_t cm_delete(struct cm_attr *mem, int flag);

int __attribute__((deprecated)) cm_try_extend(struct cm_attr *mem, size_t size);
int __attribute__((deprecated)) cm_force_extend(struct cm_attr *mem, size_t size);

ssize_t cm_get_size(struct cm_attr *mem);
ssize_t cm_get_free_size(struct cm_attr *mem);

#define CM_GROW		0x01 
#define CM_ALIGNED	0x02
#define cm_alloc(CM, ADDR, SIZE) \
	cm_do_alloc(CM, (void **)ADDR, SIZE, CM_GROW | CM_ALIGNED)
int cm_do_alloc(struct cm_attr *mem, void **addr, size_t size, int flags);
#define cm_ptr_to(CM, PTR, TO) cm_do_ptr_to(CM, (void **)PTR, TO) 
int cm_do_ptr_to(struct cm_attr *mem, void **ptr, void *to);

/* CM_RELATIVE_OFFSET:
 *	Use relative offsets. 
 * 	Occupies between:
 *	- nb_of_ptr	  bytes (at best) and 
 *	- nb_of_ptr * 6	  bytes (at worst) in CM. 
 * CM_ABSOLUTE_OFFSET:
 *	Use absolute offsets. 
 *	Occupies more memory in CM:
 *	- nb_of_ptr * 8   bytes, 
 *	but allows parallel deserialization if 
 *	the program is compiled with OpenMP.
 */
#define CM_RELATIVE_OFFSET	0x01
#define CM_ABSOLUTE_OFFSET	0x02
void *cm_serialize(struct cm_attr *mem, void *root, 
				int serialize_flags, int msync_flags);
void *cm_deserialize(void *addr, const size_t len);

#endif

