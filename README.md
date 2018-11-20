# Contiguous (and serialized) Memory Allocation
These functions allocate contiguous memory (cm) to facilitate the use of IPCs to share
complex (and large) objects containing pointers (list, binary tree, 
graph, etc).

# HOWTO
*Refer to 'examples' directory (only posix shared memory use case for the moment).*
*man pages will arrive soon.*

## Some specification and overview: 
cma is designed for 64 bits x86 little endian processor with two's complement
signed integers running GNU/Linux (LP64) and compiling with gcc C extension.

Arrival sets must be strictly identical (memory alignement) on the different
process that want to share data. Example:
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

You can't affect directly a pointer in cm, you have to use memcpy()
```
char str[] = "abc";
struct x *cm;  /* the CM */

...

cm->s = str;  /* WRONG */
memcpy(cm->s, str, str_size);  /* GOOD */
/* be sure the cm->s have a len >= at str_size */
```

In the same way, you can't affect directly a pointer in cm, from cm.
For do this and to avoid duplicating data with a memcpy, you can use the
macro cm\_ptr\_to()
```
struct cm_attr *mem;

...

cm->s2 = cm->s1;   /* WRONG */

if (cm_ptr_to(mem, &cm->s2, cm->s1))   /* GOOD */
         /* error ... */
```

