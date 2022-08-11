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
#define IN_RANGE(val, min, max) ((val) >= (min) & (val) <= (max))
#define CEIL_DIV(num, den) (((num)-1) / (den) + 1)

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

  BFG_FREE(png);
}

/* Allocates png data struct and reads png file at fpath into it using the
 * libpng API. Caller is responsible for freeing the struct with libpng_free.
 * Returns NULL on failure. */
png_data_t libpng_read(char *fpath) {
  png_data_t png = BFG_MALLOC(sizeof(struct png_data));
  if (!fpath || !png) {
    return NULL;
  }

  png->file_mode = 'r';
  png->fp = fopen(fpath, "rb");
  if (!png->fp) {
    fprintf(stderr, "Could not open file %s\n", fpath);
    return NULL;
  }

  // verify png signature in first 8 bytes
  png_byte sig[8];
  fread(sig, 1, 8, png->fp);
  if (png_sig_cmp(sig, 0, 8)) {
    fprintf(stderr, "Not a valid png file\n");
    libpng_free(png);
    return NULL;
  }

  png->png_ptr =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png->png_ptr) {
    libpng_free(png);
    return NULL;
  }

  png->info_ptr = png_create_info_struct(png->png_ptr);
  if (!png->info_ptr) {
    libpng_free(png);
    return NULL;
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

  return png;
}

/* Allocates and populates bfg data struct with data from libpng data struct.
 * Returns struct on success, NULL on failure. */
bfg_raw_t libpng_decode(png_data_t png) {
  bfg_raw_t bfg = BFG_MALLOC(sizeof(struct bfg_raw));
  if (!png || !bfg) {
    return NULL;
  }

  bfg->width = png_get_image_width(png->png_ptr, png->info_ptr);
  bfg->height = png_get_image_height(png->png_ptr, png->info_ptr);
  bfg->n_channels = png_get_channels(png->png_ptr, png->info_ptr);

  const uint32_t total_px = bfg->width * bfg->height;
  const uint32_t total_bytes = total_px * bfg->n_channels;
  if (!PROD_FITS_TYPE(bfg->width, bfg->height, UINT32_MAX) ||
      !PROD_FITS_TYPE(total_px, bfg->n_channels, UINT32_MAX)) {
    return NULL;
  }

  bfg->pixels = BFG_MALLOC(total_bytes);
  if (!bfg->pixels) {
    return NULL;
  }

  png_uint_32 row_bytes = png_get_rowbytes(png->png_ptr, png->info_ptr);
  for (png_uint_32 y = 0; y < bfg->height; y++) {
    for (png_uint_32 x = 0; x < row_bytes; x += bfg->n_channels) {
      for (png_uint_32 c = 0; c < bfg->n_channels; c++) {
        bfg->pixels[FLAT_INDEX(x + c, y, row_bytes)] = png->row_ptrs[y][x + c];
      }
    }
  }

  return bfg;
}

/* Allocates and populates libpng data struct with data from bfg data
 * struct and writes it to the file specified by fpath.
 * Returns 0 on success, nonzero on failure. */
int libpng_write(char *fpath, bfg_raw_t raw) {
  png_data_t png = BFG_MALLOC(sizeof(struct png_data));
  if (!fpath || !raw || !png) {
    return 1;
  }

  png->row_ptrs = NULL; // won't need this, don't leave uninitialized though
  png->file_mode = 'w';
  png->fp = fopen(fpath, "wb");
  if (!png->fp) {
    fprintf(stderr, "Could not write to %s\n", fpath);
    return 1;
  }

  png->png_ptr =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png->png_ptr) {
    return 1;
  }

  png->info_ptr = png_create_info_struct(png->png_ptr);
  if (!png->info_ptr) {
    return 1;
  }

  png_init_io(png->png_ptr, png->fp);

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

  png_set_IHDR(png->png_ptr, png->info_ptr, raw->width, raw->height,
               BFG_BIT_DEPTH, color_type, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png->png_ptr, png->info_ptr);

  png_uint_32 row_bytes = raw->width * raw->n_channels;
  for (png_uint_32 y = 0; y < raw->height; y++) {
    png_write_row(png->png_ptr, raw->pixels + FLAT_INDEX(0, y, row_bytes));
  }

  png_write_end(png->png_ptr, NULL);
  libpng_free(png);

  return 0;
}

// TODO: MAKE BFG FUNCS NEW FILE

/* Frees everything allocated within the bfg data struct. */
void bfg_free(/* TODO */) { /* TODO */
}

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

  const uint16_t max_block_entries = TWO_POWER(BFG_BIT_DEPTH - BFG_TAG_BITS);
  const int16_t max_diff = max_block_entries >> 1;
  const int16_t min_diff = -max_diff;

  uint32_t block_header_idx = 0;
  uint32_t block_len = 0;

  for (uint8_t c = 0; c < info->n_channels; c++) {
    bfg_block_type_t active_block = BFG_BLOCK_FULL;
    bfg_block_type_t next_block;

    uint32_t read_i = 0;
    uint8_t prev = 0;
    uint8_t curr = raw->pixels[0 + c];
    uint8_t next[2];
    next[0] = raw->pixels[n_px > 1 ? 1 * info->n_channels + c : 0 + c];
    next[1] = raw->pixels[n_px > 2 ? 2 * info->n_channels + c : n_px - 1 + c];

    int did_encode_px = 0;
    int do_change_block = 1;

    while (read_i < n_px) {
      const int8_t diff = curr - prev;
      const uint8_t next_diff = next[0] - curr;
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
        uint8_t offset_bits =
            (BFG_BIT_DEPTH - block_len * BFG_DIFF_BITS) % BFG_BIT_DEPTH;
        if (offset_bits == BFG_BIT_DEPTH - BFG_DIFF_BITS) {
          // we're at a byte boundary, so good place to switch
          if (can_start_run) {
            do_change_block = 1;
            next_block = BFG_BLOCK_RUN;
          }
        }
        if (!do_change_block && can_continue_diff) {
          did_encode_px = 1;
          block_len++;
          offset_bits = (offset_bits - BFG_DIFF_BITS) % BFG_BIT_DEPTH;
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
          int32_t block_bytes;
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

          info->n_bytes += block_bytes;
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

        active_block = next_block;
        do_change_block = 0;
      }

      // advance to next pixel
      if (did_encode_px) {
        read_i++;
        did_encode_px = 0;

        prev = curr;
        curr = next[0];
        next[0] = next[1];
        next[1] = raw->pixels[read_i < n_px - 2 ? read_i + 2 : read_i];

        // about to look at final pixel so write block after next iteration
        if (read_i == n_px - 1) {
          do_change_block = 1;
          next_block = BFG_BLOCK_NONE;
        }
      }
    }
  }

  return img;
}

bfg_raw_t bfg_decode(bfg_info_t info, bfg_img_t img) {
  bfg_raw_t raw = BFG_MALLOC(sizeof(struct bfg_raw));
  if (!info || !img || !raw) {
    return NULL;
  }

  raw->width = info->width;
  raw->height = info->height;
  raw->n_channels = info->n_channels;

  const uint32_t total_px = raw->width * raw->height;
  const uint32_t total_bytes = total_px * raw->n_channels;
  if (!PROD_FITS_TYPE(raw->width, raw->height, UINT32_MAX) ||
      !PROD_FITS_TYPE(total_px, raw->n_channels, UINT32_MAX)) {
    return NULL;
  }

  raw->pixels = BFG_MALLOC(total_bytes);
  if (!raw->pixels) {
    return NULL;
  }

  bfg_block_type_t block_type;
  uint32_t block_len = 0;
  uint32_t read_i = 0;
  uint32_t px_i = 0;
  uint8_t channel = 0;

  while (read_i < info->n_bytes) {
    assert(channel < raw->n_channels);

    const uint32_t write_i = px_i * raw->n_channels + channel;
    const uint8_t prev = px_i > 0 ? raw->pixels[write_i - raw->n_channels] : 0;

    if (block_len == 0) {
      // read block header
      block_type = (bfg_block_type_t)READ_BITS(&img[read_i], BFG_TAG_BITS,
                                               BFG_BIT_DEPTH - BFG_TAG_BITS);
      block_len = READ_BITS(&img[read_i], BFG_BIT_DEPTH - BFG_TAG_BITS, 0) + 1;

      // process header-only blocks here
      if (block_type == BFG_BLOCK_RUN) {
        for (uint32_t i = 0; i < block_len; i++) {
          raw->pixels[write_i + raw->n_channels * i] = prev;
        }
        px_i += block_len;
        block_len = 0;
      }
    } else {
      // decode block data one byte at a time
      switch (block_type) {
      case BFG_BLOCK_FULL: {
        raw->pixels[write_i] = img[read_i];
        px_i++;
        block_len--;
        break;
      }

      case BFG_BLOCK_DIFF: {
        // write out all diffs in this byte (BFG_BIT_DEPTH / BFG_DIFF_BITS)
        int8_t offset_n = 0;
        int8_t offset_bits = BFG_BIT_DEPTH - BFG_DIFF_BITS;
        int8_t diff = 0;
        while (offset_bits >= 0 && block_len > 0) {
          diff += -1 *
                  READ_BITS(&img[read_i], 1, offset_bits + BFG_DIFF_BITS - 1) *
                  READ_BITS(&img[read_i], BFG_DIFF_BITS - 1, offset_bits);
          raw->pixels[write_i + raw->n_channels * offset_n] = prev + diff;
          offset_n++;
          offset_bits -= BFG_DIFF_BITS;
          block_len--;
        }
        assert(offset_n == BFG_BIT_DEPTH / BFG_DIFF_BITS);
        px_i += offset_n;
        break;
      }

      default:
        return NULL;
      }

      if (px_i == total_px) {
        channel++;
      }

      read_i++;
    }
  }

  return raw;
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
  if (!img) {
    return NULL;
  }

  fread(img, info->n_bytes, 1, fp);

  return img;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <png file>\n", argv[0]);
    return 1;
  }

  png_data_t png = libpng_read(argv[1]);
  bfg_raw_t bfg = libpng_decode(png);
  libpng_free(png);

  libpng_write("bfg_out.png", bfg);
  bfg_free(bfg);

  return 0;
}
