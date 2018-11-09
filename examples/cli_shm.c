#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "../cma.h"

struct example {
	int x;
	char *s;
	struct example *nxt;
};

int main(void)
{
	int fd;
	struct stat st;
	struct example *cm; 

	fd = shm_open("/example_cm", O_RDWR, S_IRUSR);
	if (fd == -1) {
		perror("shm_open error");
		return 1;
	}


	if (fstat(fd, &st) == -1) {
		perror("fstat error");
		goto err;
	}	

	/* usually (and depending on your use case), it may be necessary to use
	 * a private mapping (MAP_PRIVATE) */
	cm = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (cm == MAP_FAILED) {
		perror("mmap err");
		goto err;
	}
	
	if (cm_deserialize(&cm, sizeof(*cm), st.st_size)) {
		perror("cm_deserialize");
		munmap(cm, st.st_size);
		goto err;
	}

	printf("cm->x = %d\n", cm->x);
	printf("cm->s = %s\n", cm->s);
	printf("cm->nxt->x = %d\n", cm->nxt->x);
	printf("cm->nxt->s = %s\n", cm->nxt->s);

	munmap(cm, st.st_size);
	close(fd);
	shm_unlink("/example_cm");
	return 0;
err:
	close(fd);
	shm_unlink("/example_cm");
	return 1;
}
