#include "bfg.h"
#include <assert.h>
#include <limits.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAT_INDEX(x, y, w) ((y) * (w) + (x))
#define FCLOSE(fp) ((fp) ? fclose((fp)) : 0, (fp) = NULL)
#define BIT_MASK(width, offset) ((~(~0ULL << (width)) << (offset)))
#define TWO_POWER(pow) (1 << (pow))
#define PROD_FITS_TYPE(a, b, max_val) ((a) > (max_val) / (b) ? 0 : 1)
#define IN_RANGE(val, min, max) (((val) >= (min)) & ((val) <= (max)))
#define CEIL_DIV(num, den) (((num)-1) / (den) + 1)

// TODO: DEBUG
#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)                                                   \
  (byte & 0x80 ? '1' : '0'), (byte & 0x40 ? '1' : '0'),                        \
      (byte & 0x20 ? '1' : '0'), (byte & 0x10 ? '1' : '0'),                    \
      (byte & 0x08 ? '1' : '0'), (byte & 0x04 ? '1' : '0'),                    \
      (byte & 0x02 ? '1' : '0'), (byte & 0x01 ? '1' : '0')

// write width bits of value to byte_ptr, shifted offset bits to the left
// so if *p = 0b0100001, after WRITE_BITS(p, 0b101, 3, 2), *p = 0b0110101
#define WRITE_BITS(byte_ptr, value, width, offset)                             \
  ((*(uint8_t *)(byte_ptr)) =                                                  \
       (((*(uint8_t *)(byte_ptr)) & ~BIT_MASK((width), (offset))) |            \
        (BIT_MASK((width), (offset)) & ((value) << (offset)))))

// read width bits of value from byte_ptr at offset
// so if *p = 0b0110101, READ_BITS(p, 4, 2) = 0b1101
#define READ_BITS(byte_ptr, width, offset)                                     \
  (((*(uint8_t *)(byte_ptr)) >> (offset)) & BIT_MASK((width), 0))

typedef struct png_data {
  FILE *fp;
  char file_mode; // either 'r' for read or 'w' for write
  png_structp png_ptr;
  png_infop info_ptr;
  png_bytep *row_ptrs;
} * png_data_t;

/* Frees everything allocated within the libpng data struct. */
void libpng_free(png_data_t png) {
  if (!png) {
    return;
  }

  FCLOSE(png->fp);

  if (png->row_ptrs) {
    png_uint_32 height = png_get_image_height(png->png_ptr, png->info_ptr);
    for (png_uint_32 y = 0; y < height; y++) {
      if (png->row_ptrs[y]) {
        BFG_FREE(png->row_ptrs[y]);
      }
    }
    BFG_FREE(png->row_ptrs);
  }

  if (png->info_ptr) {
    png_free_data(png->png_ptr, png->info_ptr, PNG_FREE_ALL, -1);
  }

  if (png->png_ptr) {
    if (png->file_mode == 'w') {
      png_destroy_write_struct(&png->png_ptr, &png->info_ptr);
    } else if (png->file_mode == 'r') {
      png_destroy_read_struct(&png->png_ptr, &png->info_ptr, NULL);
    }
  }
}

/* Reads png file at fpath into png data struct using the libpng API. Caller is
 * responsible for freeing the internals of the png struct with libpng_free.
 * Returns 0 on success, nonzero on failure. */
int libpng_read(char *fpath, png_data_t png) {
  if (!fpath || !png) {
    return 1;
  }

  png->file_mode = 'r';
  png->fp = fopen(fpath, "rb");
  if (!png->fp) {
    fprintf(stderr, "Could not open file %s\n", fpath);
    return 1;
  }

  // verify png signature in first 8 bytes
  png_byte sig[8];
  fread(sig, 1, 8, png->fp);
  if (png_sig_cmp(sig, 0, 8)) {
    fprintf(stderr, "Not a valid png file\n");
    libpng_free(png);
    return 1;
  }

  png->png_ptr =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png->png_ptr) {
    libpng_free(png);
    return 1;
  }

  png->info_ptr = png_create_info_struct(png->png_ptr);
  if (!png->info_ptr) {
    libpng_free(png);
    return 1;
  }

  png_init_io(png->png_ptr, png->fp);
  png_set_sig_bytes(png->png_ptr, 8); // we've already read these 8 bytes
  png_read_info(png->png_ptr, png->info_ptr);

  // libpng specifies several options for image color type:
  //   0  PNG_COLOR_TYPE_GRAY         1 channel
  //   2  PNG_COLOR_TYPE_RGB          3 channels
  //   3  PNG_COLOR_TYPE_PALETTE      1 channel
  //   4  PNG_COLOR_TYPE_GRAY_ALPHA   2 channels
  //   6  PNG_COLOR_TYPE_RGB_ALPHA    4 channels
  //
  // Encodings of type 0 and 2 may contain a tRNS chunk in the header holding a
  // single pixel value (either a two-byte gray value or three two-byte values
  // for RGB). If this chunk is present, all image pixels matching this value
  // should be transparent (alpha = 0), and all other pixels should have alpha =
  // (2^bitdepth)-1 (opaque).
  //
  // Encodings of type 3 contain a palette table (PLTE header chunk) with up to
  // 256 3-byte entries representing all possible colors in the image.
  // Individual pixel values are indexes into this table. A tRNS chunk may also
  // be present, but in this case is a table with at most one entry per palette
  // color, indicating the alpha of that color everywhere.
  //
  // I'll be honest, I was just about to implement the transformations from PLTE
  // and tRNS chunks to raw color and/or alpha values from scratch before
  // realizing png_set_expand() existed for that purpose exactly. RTFM.

  // if present, expand PLTE and tRNS tables into pixel values
  png_set_expand(png->png_ptr);

  // convert 16-bit channels to 8-bit
  png_set_strip_16(png->png_ptr);

  // for bit depths < 8, disable value packing (one value per byte)
  png_set_packing(png->png_ptr);

  // update info struct to reflect modifications
  png_read_update_info(png->png_ptr, png->info_ptr);

  png_uint_32 height = png_get_image_height(png->png_ptr, png->info_ptr);
  png_uint_32 row_bytes = png_get_rowbytes(png->png_ptr, png->info_ptr);
  png->row_ptrs = BFG_MALLOC(height * sizeof(png_bytep));
  for (png_uint_32 y = 0; y < height; y++) {
    png->row_ptrs[y] = BFG_MALLOC(row_bytes * sizeof(png_byte));
  }

  // must not happen before png_set_expand
  png_read_image(png->png_ptr, png->row_ptrs);
  png_read_end(png->png_ptr, png->info_ptr);
  FCLOSE(png->fp);

  return 0;
}

/* Populates raw image data struct with data from libpng data struct.
 * Returns 0 on success, nonzero on failure. */
int libpng_decode(png_data_t png, bfg_raw_t raw) {
  if (!png || !raw) {
    return 1;
  }

  raw->width = png_get_image_width(png->png_ptr, png->info_ptr);
  raw->height = png_get_image_height(png->png_ptr, png->info_ptr);
  raw->n_channels = png_get_channels(png->png_ptr, png->info_ptr);

  const uint32_t total_px = raw->width * raw->height;
  const uint32_t total_bytes = total_px * raw->n_channels;
  if (!PROD_FITS_TYPE(raw->width, raw->height, UINT32_MAX) ||
      !PROD_FITS_TYPE(total_px, raw->n_channels, UINT32_MAX)) {
    return 1;
  }

  raw->pixels = BFG_MALLOC(total_bytes);
  if (!raw->pixels) {
    return 1;
  }

  png_uint_32 row_bytes = png_get_rowbytes(png->png_ptr, png->info_ptr);
  for (png_uint_32 y = 0; y < raw->height; y++) {
    for (png_uint_32 x = 0; x < row_bytes; x += raw->n_channels) {
      for (png_uint_32 c = 0; c < raw->n_channels; c++) {
        raw->pixels[FLAT_INDEX(x + c, y, row_bytes)] = png->row_ptrs[y][x + c];
      }
    }
  }

  return 0;
}

/* Allocates and populates libpng data struct with data from bfg data struct and
 * writes it to the file specified by fpath.
 * Returns 0 on success, nonzero on failure. */
int libpng_write(char *fpath, bfg_raw_t raw) {
  struct png_data png;
  if (!fpath || !raw) {
    return 1;
  }

  png.row_ptrs = NULL; // won't need this, don't leave uninitialized though
  png.file_mode = 'w';
  png.fp = fopen(fpath, "wb");
  if (!png.fp) {
    fprintf(stderr, "Could not write to %s\n", fpath);
    return 1;
  }

  png.png_ptr =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png.png_ptr) {
    return 1;
  }

  png.info_ptr = png_create_info_struct(png.png_ptr);
  if (!png.info_ptr) {
    return 1;
  }

  png_init_io(png.png_ptr, png.fp);

  png_byte color_type;
  switch (raw->n_channels) {
  case 1:
    color_type = PNG_COLOR_TYPE_GRAY;
    break;
  case 2:
    color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
    break;
  case 3:
    color_type = PNG_COLOR_TYPE_RGB;
    break;
  case 4:
    color_type = PNG_COLOR_TYPE_RGB_ALPHA;
    break;
  default:
    fprintf(stderr, "Image with %d channels not supported by png\n",
            raw->n_channels);
    return 1;
  }

  png_set_IHDR(png.png_ptr, png.info_ptr, raw->width, raw->height,
               BFG_BIT_DEPTH, color_type, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png.png_ptr, png.info_ptr);

  png_uint_32 row_bytes = raw->width * raw->n_channels;
  for (png_uint_32 y = 0; y < raw->height; y++) {
    png_write_row(png.png_ptr, raw->pixels + FLAT_INDEX(0, y, row_bytes));
  }

  png_write_end(png.png_ptr, NULL);
  libpng_free(&png);

  return 0;
}

// TODO: MAKE BFG FUNCS NEW FILE

void bfg_free(bfg_raw_t raw, bfg_img_t img) {
  if (raw) {
    BFG_FREE(raw->pixels);
  }
  if (img) {
    BFG_FREE(img);
  }
}

/* Caller must free. */
bfg_img_t bfg_encode(bfg_raw_t raw, bfg_info_t info) {
  if (!raw || !info || !raw->width || !raw->height || !raw->n_channels) {
    return NULL;
  }

  info->magic_tag = BFG_MAGIC_TAG;
  info->version = BFG_VERSION;
  info->width = raw->width;
  info->height = raw->height;
  info->n_bytes = 0;
  info->n_channels = raw->n_channels;
  info->color_mode = 0; // this doesn't do anything at the moment

  const uint32_t n_px = info->width * info->height;
  uint32_t image_bytes = n_px * info->n_channels;
  if (!PROD_FITS_TYPE(info->width, info->height, UINT32_MAX) ||
      !PROD_FITS_TYPE(n_px, info->n_channels, UINT32_MAX)) {
    return NULL;
  }

  bfg_img_t img = BFG_MALLOC(image_bytes);
  if (!img) {
    return NULL;
  }

  const uint16_t max_block_entries =
      TWO_POWER(BFG_BIT_DEPTH - BFG_TAG_BITS) - 1;
  const int16_t max_diff = max_block_entries >> 1;
  const int16_t min_diff = -max_diff;

  uint32_t block_header_idx = 0;
  uint32_t block_len = 0;

  for (uint8_t c = 0; c < info->n_channels; c++) {
    bfg_block_type_t active_block = BFG_BLOCK_FULL;
    bfg_block_type_t next_block = BFG_BLOCK_NONE;

    uint32_t read_i = 0;
    uint8_t prev = 0;
    uint8_t curr = raw->pixels[c];
    uint8_t next[2];

    // account for images with fewer than 3 pixels
    next[0] = raw->pixels[n_px > 1 ? 1 * info->n_channels + c : c];
    next[1] = raw->pixels[n_px > 2 ? (unsigned)(2 * info->n_channels + c)
                                   : (unsigned)(n_px - 1 + c)];

    int did_encode_px = 0;
    int do_change_block = 0;

    // DEBUG ONLY
    // TODO: remove when done
    unsigned int n_encoded = 0;
    unsigned int n_full = 0;

    while (read_i < n_px) {
      const int8_t diff = curr - prev;
      const int8_t next_diff = next[0] - curr;
      const int8_t next_next_diff = next[1] - next[0]; // TODO: do we need
      const int can_continue_run = diff == 0;
      const int can_start_run = can_continue_run && next_diff == 0;
      const int can_continue_diff = IN_RANGE(diff, min_diff, max_diff);
      const int can_start_diff =
          can_continue_diff && IN_RANGE(next_diff, min_diff, max_diff);

      // either extend current block or switch to new block
      switch (active_block) {
      case BFG_BLOCK_FULL: {
        if (block_len > max_block_entries) {
          do_change_block = 1;
        }
        if (can_start_run) {
          do_change_block = 1;
          next_block = BFG_BLOCK_RUN;
        } else if (can_start_diff) {
          do_change_block = 1;
          next_block = BFG_BLOCK_DIFF;
        }
        if (!do_change_block) {
          did_encode_px = 1;
          block_len++;
          WRITE_BITS(&img[block_header_idx + block_len], curr, BFG_BIT_DEPTH,
                     0);
        }
        break;
      }

      case BFG_BLOCK_RUN: {
        if (block_len > max_block_entries) {
          do_change_block = 1;
          next_block = BFG_BLOCK_FULL;
        }
        if (!do_change_block && can_continue_run) {
          did_encode_px = 1;
          block_len++;
        } else if (can_start_diff) {
          do_change_block = 1;
          next_block = BFG_BLOCK_DIFF;
        } else {
          do_change_block = 1;
          next_block = BFG_BLOCK_FULL;
        }
        break;
      }

      case BFG_BLOCK_DIFF: {
        if (block_len > max_block_entries) {
          do_change_block = 1;
          next_block = BFG_BLOCK_FULL;
        }
        unsigned int offset_bits =
            (BFG_BIT_DEPTH - block_len * BFG_DIFF_BITS) % BFG_BIT_DEPTH;
        printf("offset_bits: %d\n", offset_bits);
        if (offset_bits == 0) {
          // we're at a byte boundary, so good place to switch
          if (can_start_run) {
            do_change_block = 1;
            next_block = BFG_BLOCK_RUN;
            // exit(0);
          }
        }
        if (!do_change_block && can_continue_diff) {
          did_encode_px = 1;
          block_len++;
          offset_bits = (offset_bits - BFG_DIFF_BITS) % BFG_BIT_DEPTH;
          printf("offset_bits: %d\n", offset_bits);
          uint32_t bytes_ahead =
              CEIL_DIV(block_len * BFG_DIFF_BITS, BFG_BIT_DEPTH);
          uint8_t *dest = &img[block_header_idx + bytes_ahead];
          WRITE_BITS(dest, diff < 0, 1, offset_bits + BFG_DIFF_BITS - 1);
          WRITE_BITS(dest, diff, BFG_DIFF_BITS - 1, offset_bits);
        } else {
          do_change_block = 1;
          next_block = BFG_BLOCK_FULL;
        }
        break;
      }

      default:
        break;
      }

      // switch blocks
      if (do_change_block) {
        // if block was empty we don't need to write anything
        if (block_len > 0) {
          // write old block's header
          WRITE_BITS(&img[block_header_idx], active_block, BFG_TAG_BITS,
                     BFG_BIT_DEPTH - BFG_TAG_BITS);
          WRITE_BITS(&img[block_header_idx], block_len - 1,
                     BFG_BIT_DEPTH - BFG_TAG_BITS, 0);

          // some blocks take up less than block_len bytes
          uint32_t block_bytes;
          switch (active_block) {
          case BFG_BLOCK_RUN:
            block_bytes = 0;
            break;
          case BFG_BLOCK_DIFF:
            block_bytes = CEIL_DIV(block_len * BFG_DIFF_BITS, BFG_BIT_DEPTH);
            break;
          default:
            block_bytes = block_len;
            break;
          }

          // TODO: DEBUG
          // printf("BLOCK %d LEN %d HEAD_I %d\n", active_block, block_len,
          //        block_header_idx);

          info->n_bytes += block_bytes + 1;
          block_header_idx += block_bytes + 1;
          block_len = 0;

          // realloc image if necessary
          if (block_header_idx >= image_bytes) {
            uint32_t new_image_bytes = image_bytes * 2;
            if (new_image_bytes > image_bytes) {
              image_bytes = new_image_bytes;
              img = BFG_REALLOC(img, image_bytes);
            } else {
              return NULL;
            }
          }
        }

        // TODO: DEBUG
        if (active_block == BFG_BLOCK_FULL) {
          n_full++;
        } else {
          n_encoded++;
        }

        active_block = next_block;
        do_change_block = 0;
      }

      // advance to next pixel
      if (did_encode_px) {
        // TODO: DEBUG
        // printf("PX %d:\t%d\n", read_i, curr);

        read_i++;
        did_encode_px = 0;

        prev = curr;
        curr = next[0];
        next[0] = next[1];
        if (read_i < n_px - 2) {
          next[1] = raw->pixels[(read_i + 2) * info->n_channels + c];
        }

        // about to look at final pixel so write block after next iteration
        if (read_i == n_px - 1) {
          do_change_block = 1;
          next_block = BFG_BLOCK_NONE;
          did_encode_px = 1;
        }
      }
    }

    // DEBUG: print stats
    printf("%d full, %d encoded, %d bytes\n", n_full, n_encoded, info->n_bytes);
  }

  return img;
}

int bfg_decode(bfg_info_t info, bfg_img_t img, bfg_raw_t raw) {
  if (!info || !img || !raw) {
    return 1;
  }

  raw->width = info->width;
  raw->height = info->height;
  raw->n_channels = info->n_channels;

  if (!info->width | !info->height | !info->n_channels) {
    return 1;
  }

  printf("DECODE INFO: %d %d %d %d\n", raw->width, raw->height, raw->n_channels,
         info->n_bytes);

  const uint32_t total_px = raw->width * raw->height;
  const uint32_t total_bytes = total_px * raw->n_channels;
  if (!PROD_FITS_TYPE(raw->width, raw->height, UINT32_MAX) ||
      !PROD_FITS_TYPE(total_px, raw->n_channels, UINT32_MAX)) {
    return 1;
  }

  raw->pixels = BFG_MALLOC(total_bytes);
  if (!raw->pixels) {
    return 1;
  }

  uint8_t channel = 0;
  uint32_t block_header_idx = 0;
  uint32_t px_i = 0;
  uint8_t prev = 0;

  while (block_header_idx < info->n_bytes) {
    const bfg_block_type_t block_type = (bfg_block_type_t)READ_BITS(
        &img[block_header_idx], BFG_TAG_BITS, BFG_BIT_DEPTH - BFG_TAG_BITS);
    uint32_t block_len =
        READ_BITS(&img[block_header_idx], BFG_BIT_DEPTH - BFG_TAG_BITS, 0) + 1;
    uint32_t block_bytes = 0; // set in each case below

    // printf("CHAN %d\tPX %d\tHEAD_I %d\n", channel, px_i, block_header_idx);
    printf("BLOCK %d LEN %d HEAD_I %d\n", block_type, block_len,
           block_header_idx);

    // process block
    switch (block_type) {
    case BFG_BLOCK_FULL: {
      const uint32_t block_start = block_header_idx + 1;
      block_bytes = block_len;
      const uint32_t block_end = block_start + block_bytes;
      for (uint32_t i = block_start; i < block_end; i++) {
        raw->pixels[(px_i++) * raw->n_channels + channel] = img[i];
        prev = img[i];
        printf("PX %d:\t%d\n", px_i, img[i]);
      }
      break;
    }

    case BFG_BLOCK_RUN: {
      block_bytes = 0;
      for (uint32_t i = 0; i < block_len; i++) {
        raw->pixels[(px_i++) * raw->n_channels + channel] = prev;
        printf("PX %d:\t%d\n", px_i, prev);
      }
      break;
    }

    case BFG_BLOCK_DIFF: {
      const uint32_t block_start = block_header_idx + 1;
      block_bytes = CEIL_DIV(block_len * BFG_DIFF_BITS, BFG_BIT_DEPTH);
      for (uint32_t i = block_start; i < block_start + block_bytes; i++) {
        int offset_bits = BFG_BIT_DEPTH - BFG_DIFF_BITS;

        // DEBUG: print byte as binary
        printf("%d: " BYTE_TO_BINARY_PATTERN "\n", i, BYTE_TO_BINARY(img[i]));

        while (offset_bits >= 0) {
          int8_t diff = READ_BITS(&img[i], BFG_DIFF_BITS - 1, offset_bits);
          printf("DIFF %d\n", diff);

          diff *= -1 * READ_BITS(&img[i], 1, offset_bits + BFG_DIFF_BITS - 1);
          prev += diff;
          raw->pixels[(px_i++) * raw->n_channels + channel] = prev;
          offset_bits -= BFG_DIFF_BITS;

          printf("PX %d:\t%d\n", px_i, prev);

          // we might need to end early if we've exhausted block_len
          block_len--;
          if (block_len == 0) {
            break;
          }
        }

        // exit(0);
      }
      break;
    }

    default:
      // TODO: DEBUG
      printf("\nBAD THING HAPPENED\n");
      return 1;
    }

    block_header_idx += block_bytes + 1;

    if (px_i == total_px - 1) {
      channel++;
      printf("CHAN %d\n", channel);
      px_i = 0;
    }

    // DEBUG ONLY
    if (px_i > total_px) {
      printf("\nBAD!!!!!!!!!!!!!!!!!!!!!\n");
      printf("%d %d\n", px_i, total_px);
      return 1;
    }
  }

  return 0;
}

int bfg_write(char *fpath, bfg_info_t info, bfg_img_t img) {
  FILE *fp = fopen(fpath, "wb");
  if (!fpath || !info || !img || !fp) {
    return 1;
  }

  fwrite(info, sizeof(struct bfg_info), 1, fp);
  fwrite(img, info->n_bytes, 1, fp);
  FCLOSE(fp);
  return 0;
}

bfg_img_t bfg_read(char *fpath, bfg_info_t info) {
  FILE *fp = fopen(fpath, "rb");
  if (!fpath || !info || !fp) {
    return NULL;
  }

  fread(info, sizeof(struct bfg_info), 1, fp);

  if (info->magic_tag != BFG_MAGIC_TAG) {
    printf("Not a valid bfg file\n");
    return NULL;
  }
  if (info->version != BFG_VERSION) {
    printf("Unsupported bfg version\n");
    return NULL;
  }

  bfg_img_t img = BFG_MALLOC(info->n_bytes);
  fread(img, info->n_bytes, 1, fp);
  FCLOSE(fp);
  return img;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <png file>\n", argv[0]);
    return 1;
  }

  struct png_data png;
  libpng_read(argv[1], &png);

  struct bfg_raw raw;
  libpng_decode(&png, &raw);

  struct bfg_info info;
  bfg_img_t img = bfg_encode(&raw, &info);

  bfg_write("bfg_out.bfg", &info, img);

  struct bfg_info info_in;
  bfg_img_t img_in = bfg_read("bfg_out.bfg", &info_in);

  // // print pixels
  // printf("\n");
  // for (uint32_t i = 0; i < 10; i++) {
  //   for (uint32_t j = 0; j < info_in.n_channels; j++) {
  //     printf("%d ", img_in[i * info_in.n_channels + j]);
  //   }
  //   printf("\n");
  // }
  // printf("\n");

  // // print everything out of info
  // printf("width: %d\n", info_in.width);
  // printf("height: %d\n", info_in.height);
  // printf("n_channels: %d\n", info_in.n_channels);
  // printf("n_bytes: %d\n", info_in.n_bytes);
  // printf("magic_tag: %u\n", info_in.magic_tag);
  // printf("version: %d\n", info_in.version);
  // printf("\n");

  struct bfg_raw raw_in;
  bfg_decode(&info_in, img_in, &raw_in);

  // // print everything in raw_in
  // printf("width: %d\n", raw_in.width);
  // printf("height: %d\n", raw_in.height);
  // printf("n_channels: %d\n", raw_in.n_channels);
  // printf("\n");

  // // print first 5 pixels
  // for (uint32_t i = 0; i < 10; i++) {
  //   for (uint32_t j = 0; j < raw_in.n_channels; j++) {
  //     printf("%d ", raw_in.pixels[i * raw_in.n_channels + j]);
  //   }
  //   printf("\n");
  // }

  libpng_write("bfg_out.png", &raw_in);

  libpng_free(&png);
  bfg_free(&raw, img);
  bfg_free(&raw_in, img_in);

  return 0;
}
