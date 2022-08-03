#define BFG_MAGIC 0xBF6F

/* Optionally provide custom malloc and free implementations. */
#ifndef BFG_MALLOC
#define BFG_MALLOC(sz) malloc(sz)
#endif
#ifndef BFG_FREE
#define BFG_FREE(ptr) free(ptr)
#endif
