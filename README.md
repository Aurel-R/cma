# Contiguous (and serialized) Memory Allocation
These functions allocate contiguous memory (cm) in order to share complex (and large) objects containing pointers (list, tree, graph, etc).

# HOWTO
*Refer to 'examples' directory (only posix shared memory use case for the moment).*
*man pages will arrive soon.*

## Some specification and overview
cma is designed for 64 bits x86 little endian processor with two's complement signed integers, running LP64 GNU/Linux system (8-byte aligned pointer) with flat address space and compiling with gcc C extension (-std=gnu11).

Arrival sets must be strictly identical (memory alignment) on the different processes that want to share data. Example:
```
/* srv app */
struct data_srv {  /* a stucture example */
        int x;
        char *s;
        struct data_srv *next;
};

...

/* cli app */
struct data_cli {   /* WRONG */
        int x;
        struct data_cli *next;
        char *s;
};

struct data_cli {    /* GOOD */
        int x;
        char *s;
        struct data_cli *next;
};
```

You can't affect directly a pointer in cm from cm. You have to use `cm_ptr_to()`
```
struct cm_attr *mem;

...

cm->s2 = cm->s1;   /* WRONG */

if (cm_ptr_to(mem, &cm->s2, cm->s1))   /* GOOD */
         /* error ... */
```

## Possible improvement

- The current implementation only supports a maximum of 2GB for both serialization modes. `CM_ABSOLUTE_OFFSET` can technically support an allocation size of serval terabytes with little code modification (do not calculate a vlq relative offset in `cm_grow()`)


- The current allocator is basic. Write a custom allocator to have possibility to free an elem in cm and reuse his space (which works like a traditional memory allocator, in short).

- Implement an alternative method:
	  Message-based synchronization to choose a fixed addr between the participants:
    * (++)  Remove serialization/deserialization
    * (+)   Allow 'duplex transfert' (real-time sharing):
         * Locally (shared memory)
         * Remotely in cluster? (RDMA, DSM, SSI, etc..)
   * (-?)  If 'duplex transfert', user need to use lock/mutex on memory against concurrent accesses (like classic shm)
   * (--)  **Is not guaranteed to converge** ! (find the same addr on each participant)


