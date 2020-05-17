CC=gcc
CFLAGS=-std=gnu11 -O3 -march=native -Wall -Wextra
D_CFLAGS=-std=gnu11 -g -Wall -Wextra 
LDFLAGS=-lrt
ALL=list_serialize list_deserialize

#export OMP_NUM_THREADS=4

all: $(ALL) 

list_serialize: list_serialize.c ../cma.c
	$(CC) $(CFLAGS) -fopenmp -o $@ $^ $(LDFLAGS) 
list_deserialize: list_deserialize.c ../cma.c
	$(CC) $(CFLAGS) -fopenmp -o $@ $^ $(LDFLAGS) 

clean:
	rm $(ALL)
