/*

BFG - Bloody Fast Graphics format for lossless image compression.

Seb Seager


## License

Copyright (c) 2022, Seb Seager.

<>


## Motivation

<>


## Format

TODO<>



##

*/

/* Optionally provide custom malloc and free implementations. */
#ifndef BFG_MALLOC
#define BFG_MALLOC(sz) malloc(sz)
#endif
#ifndef BFG_FREE
#define BFG_FREE(ptr) free(ptr)
#endif

#define BFG_MAGIC_WORD (0xBF6F)
#define BFG_BIT_DEPTH (8)

typedef enum {
  GRAY,
  GRAY_ALPHA,
  RGB,
  RGB_ALPHA,
} bfg_color_type_t;

typedef struct bfg_data {
  unsigned long long width;
  unsigned long long height;
  unsigned int n_channels;
  bfg_color_type_t color_type;
  unsigned char *pixels;
} * bfg_data_t;
