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
	char *s;
	int x;
	struct example *nxt;
};

struct example *foo(void)
{
	struct example *ur;
	char str[] = "toto";
	
	if (cma(&ur, sizeof(*ur)))
		return NULL;
	ur->x = 1;

	if (cma(&ur->s, 5 * sizeof(*ur->s)))
		return NULL;
	memcpy(ur->s, str, 5);

	if (cma(&ur->nxt, sizeof(*ur->nxt)))
		return NULL;
	ur->nxt->x = 2;
	
	if (ptr_to(&ur->nxt->s, ur->s))
		return NULL;
	
	return cm_sync(MS_SYNC);
}

int main(void)
{
	int fd;
	struct example *cm;

	fd = shm_open("/example_cm", O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd == -1) {
		perror("shm_open error");
		return 1;
	}

	cm_set_properties(fd, MAP_SHARED, 0);

	cm = foo();
	if (!cm)
		goto err;

	printf("cm->x = %d\n", cm->x);
	printf("cm->s = %s\n", cm->s);
	printf("cm->nxt->x = %d\n", cm->nxt->x);
	printf("cm->nxt->s = %s\n", cm->nxt->s);

	cm_free(DELETE_MAP);
	close(fd);
	return 0;
err:
	cm_free(DELETE_MAP);
	shm_unlink("/example_cm");
	close(fd);
	perror("cma error");
	return 1;
}
