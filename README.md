# First prototype (11 March 2016)
This API allocate contiguous memory (CM) to perform IPC mechanism for share
complex (and large) objects containing pointers (like list or binary tree for
example).
It is based on pointers arithmetic to not use data duplication and serialization.

# HOWTO
*Refer to 'examples' directory (only posix shared memory for the moment)*

## Some specification and overview: 
Arrived set have to be exactly the same for the server and the client
(memory alignement too)
```
/* srv app */
strcut data_srv {  /* a stucture example */
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

- **void cm\_set\_properties(int fd, mode\_t mode)**
  Only shared memory and mapped file are concerning. It define the fd and mode
  for mapping (defaults values are *MAP\_ANON | MAP\_SHARED*).

- **cma(ADDR, X) MACRO** (int cm\_allocator(void \*\*addr, size\_t size, int flag))
  Allocate contiguous memory. *ADDR* is the address pointer of object and *X* the size.
  Function return 0 on success and -1 on error.

- **ptr\_to(P, X) MACRO** (int affect\_ptr(void \*\*ptr, void \*to))
  Initialize a pointer at the address *P* from a pointer *X* (in CM).
  Return 0 on success and -1 on error:

You can't affect directly a pointer in CM, you have to use memcpy()
```
char str[] = "abc";
struct x *cm;  /* the CM */

...

cm->s = str;  /* WRONG */
memcpy(cm->s, str, str_size);  /* GOOD */
/* be sure the cm->s have a len >= at str_size */
```

In the same way, you can't affect directly a pointer in CM, from CM.
For do this and do not duplicate the datas with a memcpy, you can use the
macro ptr\_to()
```
cm->s2 = cm->s1;   /* WRONG */

if (ptr_to(&cm->s2, cm->s1))   /* GOOD */
         /* error ... */
```

- **size\_t cm\_get\_size(void)** and **size\_t cm\_get\_pre\_size(void)**
  Return the actual size of the CM. *cm\_get\_pre\_size* can be useful for
  function like send: *send(sock, cm\_sync(0), cm\_get\_pre\_size(), 0);* because
  it return the size of the CM should have after a cm\_sync()

- **void \*cm\_sync(int flags)**
  Synchronize the CM. Return a pointer at the start of CM or NULL on error.
  *flags* are used for *msync(2)* (only for shared memory and mapped file).

- **int cm\_free(void)**
  Free the contiguous memory and reset his properties.

- **cm\_processing(X, O\_SIZE, D\_SIZE) MACRO** (call void cm\_processing\_r(void \*\*addr, size\_t object\_size, size\_t data\_size))
  Used on client application just after getting the buffer (addr). The memory
  containing the buffer have to be wrtieable (in shm and mapped file).
  *X* is the address of buffer, *O\_SIZE* the size of the object (struct) and *D\_SIZE*
  the data size (obtained by a *fstat()* in shm for example).

###### Some useful functions:
- shm\_open
- fstat
- fchown
- fchmod
- mprotect
- mlock
- madvise
- fcntl
- close
- mmap
- munmap
- shm_unlink
