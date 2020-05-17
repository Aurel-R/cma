/*
 * Usage: 
 *	./list_serialize > serialize.out	# serialize
 *	./list_deserialize > deserialize.out	# deserialize
 *	md5sum serialize.out deserialize.out	# check
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "../cma.h"

#define SORTED	0
#define SIZE	200000000	// 200 MB 

#define cm_alloc	cm_default_alloc
#define cm_free		cm_default_free

struct node {
	unsigned x;
	struct node *nxt;
};

#define START	1
#define STOP	0
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

static struct node *insert(struct node *head, struct node *curr)
{
	struct node *n, *tmp = head;

	if (!head)
		return curr;

	if (curr->x <= head->x) {
		curr->nxt = head;
		head = curr;
		goto end;
	}

	for (n = head->nxt; n; n = n->nxt) {
		if (curr->x <= n->x) {
			tmp->nxt = curr;
			curr->nxt = n;
			goto end;
		}
		tmp = n;
	}

	tmp->nxt = curr;
	curr->nxt = NULL;
end:
	return head;
}

struct node *create_list_sorted(struct cm_attr *mem)
{	
	size_t i;
	struct node *curr, *head = NULL;

	srand(time(NULL));
	for (i = 0; i < 100000ul; i++) {
		curr = cm_alloc(mem, sizeof(*curr));
		if (!curr)
			return NULL;
		cm_add_ptr(mem, &curr->nxt);
		curr->x = (unsigned)rand() % 100ul;
		curr->nxt = NULL;
		head = insert(head, curr);
	}
	
	bench(START);
	if (!cm_serialize(mem, head, 0))
		return NULL;	
	bench(STOP);

	return head;
}

struct node *create_list(struct cm_attr *mem)
{	
	size_t i;
	struct node *curr, *head = NULL;

	srand(time(NULL));
	for (i = 0; i < 10000000ul; i++) {
		curr = cm_alloc(mem, sizeof(*curr));
		if (!curr)
			return NULL;
		cm_add_ptr(mem, &curr->nxt);
		curr->x = (unsigned)rand() % 100000ul;
		curr->nxt = head;
		head = curr;
	}
	
	bench(START);
	if (!cm_serialize(mem, head, 0))
		return NULL;	
	bench(STOP);

	return head;
}

int main(void)
{
	int fd;
	struct cm_attr *mem;
	struct node *head, *n;

	shm_unlink("/example_cm");
	fd = shm_open("/example_cm", O_CREAT|O_EXCL|O_RDWR, S_IRUSR|S_IWUSR);
	if (fd == -1) {
		perror("shm_open");
		return 1;
	}

	mem = cm_create(fd, SIZE, MAP_SHARED);
	if (!mem) {
		perror("cm_create");
		close(fd);
		return 1;
	}

	if (SORTED)
		head = create_list_sorted(mem);
	else 
		head = create_list(mem);

	if (!head)
		goto err;
	
	for (n = head; n; n = n->nxt)
		printf("%d\n", n->x);

	cm_delete(mem);
	close(fd);
	return 0;
err:
	perror("cma");
	cm_delete(mem);
	close(fd);
	shm_unlink("/example_cm");
	return 1;
}

