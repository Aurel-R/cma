all: srv_shm cli_shm

srv_shm: ../cma.h ../cma.c srv_shm.c
	gcc -Wall -o srv_shm -lrt -D_GNU_SOURCE srv_shm.c ../cma.c 
cli_shm: ../cma.h ../cma.c cli_shm.c
	gcc -Wall -o cli_shm -lrt -D_GNU_SOURCE cli_shm.c ../cma.c

clean:
	rm *.o srv_shm cli_shm
