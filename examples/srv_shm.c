#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "../cma.h"

struct example {
	int x;
	char *s;
	struct example *nxt;
};

struct example *foo(struct cm_attr *mem)
{
	struct example *ur;
	char str[] = "toto";
	
	if (cm_alloc(mem, &ur, sizeof(*ur)))
		return NULL;
	ur->x = 1;
	
	if (cm_alloc(mem, &ur->s, 5 * sizeof(*ur->s)))
		return NULL;
	memcpy(ur->s, str, 5);
	
	if (cm_alloc(mem, &ur->nxt, sizeof(*ur->nxt)))
		return NULL;
	ur->nxt->x = 2;
	
	if (cm_ptr_to(mem, &ur->nxt->s, ur->s))
		return NULL;

/*	cm_ptr_to(mem, &ur->nxt->s, NULL); */
	return cm_serialize(mem, MS_SYNC);
}

int main(void)
{
	int fd;
	struct example *cm;
	struct cm_attr *mem;

	shm_unlink("/example_cm");
	fd = shm_open("/example_cm", O_CREAT|O_EXCL|O_RDWR, S_IRUSR|S_IWUSR);
	if (fd == -1) {
		perror("shm_open error");
		return 1;
	}

	mem = cm_create(fd, 4096, MAP_SHARED, 0);
	if (!mem) {
		perror("cm_create");
		close(fd);
		return 1;
	}

	cm = foo(mem);
	if (!cm)
		goto err;
	
	printf("cm->x = %d\n", cm->x);
	printf("cm->s = %s\n", cm->s);
	printf("cm->nxt->x = %d\n", cm->nxt->x);
	printf("cm->nxt->s = %s\n", cm->nxt->s);

	cm_delete(mem, CM_EREASE);
	close(fd);
	return 0;
err:
	perror("cma");
	cm_delete(mem, CM_EREASE);
	close(fd);
	shm_unlink("/example_cm");
	return 1;
}
