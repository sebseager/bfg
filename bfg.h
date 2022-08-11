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

- start out with header type NONE
- if you're in NONE type, go ahead and start a new FULL block, set its
count to 1, and write the 8-bit pixel in the next byte
- let the current pixel value = x
- if RUN
  - if x == run value increment the run and move on
  - elif x is within +/- 7 of prev, check next value: if it is also within
  delta range then start DELTA, write both chunks of 4 bits, and advance by 2
  - else start FULL and write value
- elif DELTA
  - if x == prev val
    - if we're halfway through a delta byte, ignore this and write out 0000 to
finish it
    - if we just finished a byte, check the next value: if it also == prev_val
    then start a RUN, set to 2, and advance by 2
  - if x is within delta, add 4 bits and increment counter

  *** IS THIS RIGHT ***
  - if x is outside delta, check next value, if it's equal then start RUN, if
it's within delta start DELTA, otherwise start FULL

- elif FULL
  - TODO



// - if you encounter a pixel value that's the same as your stored previous
//   - if you're in a RUN, increment it obviously
//   - if yo
//   - if you're in a FULL, check the diff with the next pixel
//     - if it's also the same, start a SHORT RUN and set its count to 1

//   - if you're in any kind of DELTA and this would complete a byte, just fill
it with 0000
//   - if you're in a DELTA and this would start a new byte (divisible by 8),
start a SHORT RUN and set its count to 1

//   check the next byte (if you can)
//     - if it's also the same, start a SHORT RUN and set its count to 1
//     (NOT 2, the next iteration will increment)
//     - if NOT,



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
#define BFG_CHANNEL_DEPTH_BYTES (1)
#define BFG_HEADER_TAG_BITS (3)
#define BFG_DIFF_BITS (4) // MUST BE DIVISIBLE BY 8

/* Tags appearing in the first BFG_HEADER_TAG_BITS of each block header. */
typedef enum {
  BFG_BLOCK_FULL = 0,
  BFG_BLOCK_RUN = 1,
  BFG_BLOCK_DIFF_PREV = 2,
} bfg_block_type_t;

typedef struct bfg_raw {
  uint32_t width;
  uint32_t height;
  uint8_t n_channels;
  uint8_t *pixels;
} * bfg_raw_t;

typedef struct bfg_encoded {
  uint32_t magic_tag;
  uint32_t width;
  uint32_t height;
  uint8_t n_channels;
  uint8_t byte_depth;
  uint8_t color_mode;
  uint8_t *image;
} * bfg_encoded_t;
