/*

BFG2 - Fast lossless image compression.

Seb Seager

## License

Copyright (c) 2022, Seb Seager.

## Format

Header (16 bytes, little-endian):
  Bytes 0-3:   Magic "BFG2" (0x42, 0x46, 0x47, 0x32)
  Bytes 4-7:   Width  (uint32)
  Bytes 8-11:  Height (uint32)
  Byte  12:    Channels (3 = RGB, 4 = RGBA)
  Bytes 13-15: Reserved (zero)

Pixel data is a sequence of byte-aligned ops:

  0xxxxxxx                  DELTA1  (1 byte)  Luma-correlated small delta:
                                      dg[-4..3], (dr-dg)[-2..1], (db-dg)[-2..1]
  10xxxxxx + 1 byte         DELTA2  (2 bytes) Luma-correlated medium delta:
                                      dg[-32..31], (dr-dg)[-8..7], (db-dg)[-8..7]
  110xxxxx                  RUN     (1 byte)  run length 1..32
  1110xxxx                  CACHE   (1 byte)  cache index 0..15
  11110000 + r + g + b      RGB     (4 bytes) literal RGB, alpha unchanged
  11110001 + r + g + b + a  RGBA    (5 bytes) literal RGBA
  11110010 + 1 byte         RUN2    (2 bytes) extended run length 33..288

Prediction: average of left and above pixels per channel.
  - First pixel: predict {0, 0, 0, 255}
  - First row (y=0, x>0): predict = left pixel
  - First column (x=0, y>0): predict = above pixel
  - Interior: predict = (left + above) / 2 per channel

Delta ops encode luma-correlated residuals: green delta directly,
red and blue as offsets from green delta. This leverages the
correlation between color channels in natural images.
A 16-entry hash cache stores recently seen pixel values.

*/

#ifndef BFG_H
#define BFG_H

#include <stdint.h>
#include <stdlib.h>

/* Header magic bytes: "BFG2" */
#define BFG_MAGIC (0x32474642u) /* little-endian for "BFG2" */
#define BFG_HEADER_SIZE 16
#define BFG_MAX_PIXELS ((uint32_t)400000000) /* ~400 megapixels */
#define BFG_CACHE_SIZE 16

/* Op tag masks */
#define BFG_OP_DELTA1 0x00 /* 0xxxxxxx */
#define BFG_OP_DELTA2 0x80 /* 10xxxxxx */
#define BFG_OP_RUN    0xC0 /* 110xxxxx */
#define BFG_OP_CACHE  0xE0 /* 1110xxxx */
#define BFG_OP_RGB    0xF0 /* 11110000 */
#define BFG_OP_RGBA   0xF1 /* 11110001 */
#define BFG_OP_RUN2   0xF2 /* 11110010 + 1 byte: extended run 33..288 */

#define BFG_MASK1     0x80 /* 1-bit prefix mask */
#define BFG_MASK2     0xC0 /* 2-bit prefix mask */
#define BFG_MASK3     0xE0 /* 3-bit prefix mask */
#define BFG_MASK4     0xF0 /* 4-bit prefix mask */

/* Optionally provide custom malloc and free. */
#ifndef BFG_MALLOC
#define BFG_MALLOC(sz) malloc(sz)
#define BFG_FREE(ptr) free(ptr)
#endif

/* RGBA pixel for internal processing. */
typedef struct {
  uint8_t r, g, b, a;
} bfg_pixel_t;

/* Internal raw image representation. */
typedef struct bfg_raw {
  uint32_t width;
  uint32_t height;
  uint8_t n_channels;
  uint8_t *pixels;
} * bfg_raw_t;

/* File header (16 bytes). */
typedef struct bfg_header {
  uint32_t magic;
  uint32_t width;
  uint32_t height;
  uint8_t channels;
  uint8_t reserved[3];
} bfg_header_t;

/* Encoded image data. */
typedef uint8_t *bfg_img_t;

/* Encode raw pixels into BFG format. Returns encoded data (caller frees).
 * header is filled with image metadata. Returns NULL on failure. */
bfg_img_t bfg_encode(bfg_raw_t raw, bfg_header_t *header, uint32_t *out_len);

/* Decode BFG data into raw pixels. raw->pixels is allocated (caller frees).
 * Returns 0 on success, nonzero on failure. */
int bfg_decode(const bfg_header_t *header, const uint8_t *data,
               uint32_t data_len, bfg_raw_t raw);

/* Write BFG file (header + data). Returns 0 on success. */
int bfg_write(const char *fpath, const bfg_header_t *header,
              const uint8_t *data, uint32_t data_len);

/* Read BFG file. Returns encoded data (caller frees). header and out_len
 * are filled. Returns NULL on failure. */
uint8_t *bfg_read(const char *fpath, bfg_header_t *header, uint32_t *out_len);

/* Free raw pixels and/or encoded data. Either pointer may be NULL. */
void bfg_free_raw(bfg_raw_t raw);
void bfg_free_img(bfg_img_t img);

#endif /* BFG_H */
