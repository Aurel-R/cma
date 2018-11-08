CC=gcc
CFLAGS=-std=gnu11 -O3 -fno-strict-aliasing -fno-strict-overflow -Wall -Wextra
LDFLAGS=-lrt -D_GNU_SOURCE
ALL=srv_shm cli_shm

all: $(ALL) 

srv_shm: srv_shm.c ../cma.c
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $^
cli_shm: cli_shm.c ../cma.c
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $^
clean:
	rm $(ALL)
