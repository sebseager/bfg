#include "bfg.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAT_INDEX(x, y, w) ((y) * (w) + (x))
#define FCLOSE(fp) ((fp) ? fclose(fp) : 0, (fp) = NULL)

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
    for (int y = 0; y < height; y++) {
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

/*
 * Allocates png data struct and reads png file at fpath into it using the
 * libpng API. Caller is responsible for freeing the struct with libpng_free.
 * Returns NULL on failure.
 */
png_data_t libpng_read(char *fpath) {
  png_data_t png = BFG_MALLOC(sizeof(struct png_data));
  if (!png) {
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
    png->row_ptrs[y] = BFG_MALLOC(row_bytes);
  }

  // must not happen before png_set_expand
  png_read_image(png->png_ptr, png->row_ptrs);
  png_read_end(png->png_ptr, png->info_ptr);
  FCLOSE(png->fp);

  return png;
}

/*
 * Allocates and populates bfg data struct with data from libpng data struct.
 * Returns struct on success, NULL on failure.
 */
bfg_raw_t libpng_decode(png_data_t png) {
  if (!png) {
    return NULL;
  }

  bfg_raw_t bfg = BFG_MALLOC(sizeof(struct bfg_raw));
  if (!bfg) {
    return NULL;
  }

  bfg->width = png_get_image_width(png->png_ptr, png->info_ptr);
  bfg->height = png_get_image_height(png->png_ptr, png->info_ptr);
  bfg->n_channels = png_get_channels(png->png_ptr, png->info_ptr);

  bfg->pixels = BFG_MALLOC(bfg->width * bfg->height * bfg->n_channels);
  if (!bfg->pixels) {
    return NULL;
  }

  png_uint_32 row_bytes = png_get_rowbytes(png->png_ptr, png->info_ptr);
  for (png_uint_32 y = 0; y < bfg->height; y++) {
    for (png_uint_32 x = 0; x < row_bytes; x += bfg->n_channels) {
      for (unsigned int c = 0; c < bfg->n_channels; c++) {
        bfg->pixels[FLAT_INDEX(x + c, y, row_bytes)] = png->row_ptrs[y][x + c];
      }
    }
  }

  return bfg;
}

/*
 * Allocates and populates libpng data struct with data from bfg data
 * struct and writes it to the file specified by fpath.
 * Returns 0 on success, nonzero on failure
 */
int libpng_write(char *fpath, bfg_raw_t bfg) {
  if (!bfg) {
    return 1;
  }

  png_data_t png = BFG_MALLOC(sizeof(struct png_data));
  if (!png) {
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
  switch (bfg->n_channels) {
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
            bfg->n_channels);
    return 1;
  }

  png_set_IHDR(png->png_ptr, png->info_ptr, bfg->width, bfg->height,
               BFG_BIT_DEPTH, color_type, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png->png_ptr, png->info_ptr);

  png_uint_32 row_bytes = bfg->width * bfg->n_channels;
  for (png_uint_32 y = 0; y < bfg->height; y++) {
    png_write_row(png->png_ptr, bfg->pixels + FLAT_INDEX(0, y, row_bytes));
  }

  png_write_end(png->png_ptr, NULL);
  libpng_free(png);

  return 0;
}

// TODO: BFG FUNCTIONS

/* Frees everything allocated within the bfg data struct. */
void bfg_free(bfg_raw_t bfg) {
  if (!bfg) {
    return;
  }

  BFG_FREE(bfg->pixels);
  BFG_FREE(bfg);
}

int bfg_encode(bfg_raw_t bfg, uint8_t *encoded) {}

bfg_raw_t bfg_read(char *fpath) { return 0; }

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
