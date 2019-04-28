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
#include <time.h>
#include <omp.h>
#include "../cma.h"

#define START	1
#define STOP	0

struct node {
	unsigned x;
	struct node *nxt;
};

static void bench(int flag)
{
	static struct timespec s;
	struct timespec e, res;

	clock_gettime(CLOCK_MONOTONIC, &e);

	if (flag & START) {
		s = e;
		return;
	}

	res.tv_sec  = e.tv_sec - s.tv_sec;
	res.tv_nsec = e.tv_nsec - s.tv_nsec;

	if (res.tv_sec > 0 && res.tv_nsec < 0) {
		res.tv_sec--;
		res.tv_nsec += 1e9;
	} 

	fprintf(stderr, "%02ld.%09ld\n", res.tv_sec, res.tv_nsec);
}

int main(void)
{
	int fd;
	void *cm;
	struct stat st;
	struct node *head, *n;

	fd = shm_open("/example_cm", O_RDONLY, S_IRUSR);
	if (fd == -1) {
		perror("shm_open");
		return 1;
	}

	if (fstat(fd, &st) == -1) {
		perror("fstat");
		goto err;
	}	

	cm = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (cm == MAP_FAILED) {
		perror("mmap");
		goto err;
	}
		
	bench(START);
	head = cm_deserialize(cm, st.st_size);
	if (!head) {
		perror("cm_deserialize");
		munmap(cm, st.st_size);
		goto err;
	}
	bench(STOP);

	for (n = head; n; n = n->nxt)
		printf("%d\n", n->x);

	munmap(cm, st.st_size);
	close(fd);
	shm_unlink("/example_cm");
	return 0;
err:
	close(fd);
	shm_unlink("/example_cm");
	return 1;
}

