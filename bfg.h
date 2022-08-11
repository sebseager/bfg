/*

BFG - Bloody Fast Graphics format for lossless image compression.

Seb Seager


## License

Copyright (c) 2022, Seb Seager.

<>


## Motivation

<>


## Format

<>

##

*/

#include <stdint.h>

/* Optionally provide custom malloc and free implementations. */
#ifndef BFG_MALLOC
#define BFG_MALLOC(sz) malloc(sz)
#endif
#ifndef BFG_REALLOC
#define BFG_REALLOC(ptr, sz) realloc(ptr, sz)
#endif
#ifndef BFG_FREE
#define BFG_FREE(ptr) free(ptr)
#endif

#define BFG_MAGIC_TAG (0xBFBFBFBF)
#define BFG_BIT_DEPTH (8)
#define BFG_HEADER_TAG_BITS (3)
#define BFG_DIFF_BITS (4) // must be divisible by BFG_BIT_DEPTH

/* Tags appearing in the first BFG_HEADER_TAG_BITS of each block header. No tag
 * with a value < 0 should ever appear in a valid BFG file. These are provided
 * for ease of implementation only. */
typedef enum {
  BFG_BLOCK_NONE = -1,
  BFG_BLOCK_FULL = 0,
  BFG_BLOCK_RUN = 1,
  BFG_BLOCK_DIFF_PREV = 2,
} bfg_block_type_t;

/* Internal image representation provided to bfg_encode. */
typedef struct bfg_raw {
  uint32_t width;
  uint32_t height;
  uint8_t n_channels;
  uint8_t *pixels;
} * bfg_raw_t;

/* Encoded BFG representation, as written to file. */
typedef struct bfg_encoded {
  uint32_t magic_tag;
  uint32_t width;
  uint32_t height;
  uint32_t n_bytes;
  uint8_t n_channels;
  uint8_t color_mode;
  uint8_t *image;
} * bfg_encoded_t;
