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

#include <stdint.h>

/* Optionally provide custom malloc and free implementations. */
#ifndef BFG_MALLOC
#define BFG_MALLOC(sz) malloc(sz)
#endif
#ifndef BFG_FREE
#define BFG_FREE(ptr) free(ptr)
#endif

#define BFG_MAGIC_TAG ({'b', 'f', 'g', 'f'})
#define BFG_BIT_DEPTH (8)

typedef enum __attribute__((packed)) {
  GRAY,
  GRAY_ALPHA,
  RGB,
  RGB_ALPHA,
} bfg_color_type_t;

typedef struct bfg_raw {
  uint32_t width;
  uint32_t height;
  uint8_t n_channels;
  uint8_t *pixels;
} * bfg_raw_t;

typedef struct bfg_encoded {
  uint8_t magic_tag[4];
  uint32_t width;
  uint32_t height;
  uint8_t n_channels;
  uint8_t channel_depth;
  uint8_t color_mode; // 

} * bfg_encoded_t;
