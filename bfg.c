#include "bfg.h"
#include "png.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>

#define FLAT_INDEX(x, y, w) ((y) * (w) + (x))

typedef struct png_data {
  png_structp png_ptr;
  png_infop info_ptr;
  png_bytep *row_ptrs;
} * png_data_t;

/*
 * Allocates png data struct and reads png file at fpath into it using the
 * libpng API. Caller is responsible for freeing the struct with libpng_free.
 */
png_data_t libpng_read(char *fpath) {
  FILE *fp = fopen(fpath, "rb");
  if (!fp) {
    fprintf(stderr, "Could not open file %s\n", fpath);
    return NULL;
  }

  png_data_t png = malloc(sizeof(struct png_data));

  // verify png signature in first 8 bytes
  png_byte sig[8];
  fread(sig, 1, 8, fp);
  if (png_sig_cmp(sig, 0, 8)) {
    fprintf(stderr, "Not a valid png file\n");
    fclose(fp);
    return NULL;
  }

  png->png_ptr =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png->png_ptr) {
    fprintf(stderr, "png_create_read_struct failed\n");
    fclose(fp);
    return NULL;
  }

  png->info_ptr = png_create_info_struct(png->png_ptr);
  if (!png->info_ptr) {
    fprintf(stderr, "png_create_info_struct failed\n");
    png_destroy_read_struct(&png->png_ptr, NULL, NULL);
    fclose(fp);
    return NULL;
  }

  png_init_io(png->png_ptr, fp);
  png_set_sig_bytes(png->png_ptr, 8); // we've already read these 8 bytes
  png_read_info(png->png_ptr, png->info_ptr);

  // don't access info_ptr directly
  png_uint_32 height = png_get_image_height(png->png_ptr, png->info_ptr);
  png->row_ptrs = malloc(sizeof(png_bytep) * height);
  for (png_uint_32 y = 0; y < height; y++) {
    png->row_ptrs[y] = malloc(png_get_rowbytes(png->png_ptr, png->info_ptr));
  }

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

  png_set_expand(png->png_ptr);
  png_set_strip_16(png->png_ptr);
  png_read_update_info(png->png_ptr, png->info_ptr);

  // must not happen before png_set_expand
  png_read_image(png->png_ptr, png->row_ptrs);

  fclose(fp);
  return png;
}

/*
 * Allocates and populates bfg data struct with data from libpng data struct.
 * Returns NULL on failure.
 */
bfg_data_t libpng_decode(png_data_t png) {
  bfg_data_t bfg = malloc(sizeof(struct bfg_data));
  if (!bfg) {
    fprintf(stderr, "bfg_data malloc failed\n");
    return NULL;
  }

  bfg->width = png_get_image_width(png->png_ptr, png->info_ptr);
  bfg->height = png_get_image_height(png->png_ptr, png->info_ptr);
  bfg->n_channels = png_get_channels(png->png_ptr, png->info_ptr);

  png_byte color_type = png_get_color_type(png->png_ptr, png->info_ptr);
  switch (color_type) {
  case PNG_COLOR_TYPE_GRAY:
    bfg->color_type = GRAY;
    break;
  case PNG_COLOR_TYPE_GRAY_ALPHA:
    bfg->color_type = GRAY_ALPHA;
    break;
  case PNG_COLOR_TYPE_RGB:
    bfg->color_type = RGB;
    break;
  case PNG_COLOR_TYPE_RGB_ALPHA:
    bfg->color_type = RGB_ALPHA;
    break;
  default:
    fprintf(stderr, "Unsupported color type %d\n", color_type);
    return NULL;
  }

  bfg->pixels = malloc(bfg->width * bfg->height * bfg->n_channels);
  if (!bfg->pixels) {
    fprintf(stderr, "bfg_data pixels malloc failed\n");
    return NULL;
  }

  png_uint_32 row_bytes = bfg->width * bfg->n_channels;
  for (png_uint_32 y = 0; y < bfg->height; y++) {
    for (png_uint_32 x = 0; x < row_bytes; x += bfg->n_channels) {
      for (int c = 0; c < bfg->n_channels; c++) {
        bfg->pixels[FLAT_INDEX(x + c, y, row_bytes)] = png->row_ptrs[y][x + c];
      }
    }
  }

  return bfg;
}

/*
 * Allocates and populates libpng data struct with data from bfg data
 * struct. Returns NULL on failure.
 */
png_data_t libpng_encode(bfg_data_t bfg) { return NULL; }

/*
 * Writes new png file at fpath from given libpng data struct.
 * Returns 0 on success, 1 on failure.
 */
int libpng_write(char *fpath, png_data_t png) { return 0; }

/* Frees everything allocated within the data struct by libpng. */
void libpng_free(png_data_t png) {
  png_free_data(png->png_ptr, png->info_ptr, PNG_FREE_ALL, -1);
}

bfg_data_t bfg_read(char *fpath) { return 0; }

int bfg_write(char *fpath, bfg_data_t bfg) { return 0; }

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <png file>\n", argv[0]);
    return 1;
  }

  png_data_t png = libpng_read(argv[1]);

  if (!png) {
    return 1;
  }

  png_uint_32 height = png_get_image_height(png->png_ptr, png->info_ptr);
  png_uint_32 width = png_get_image_width(png->png_ptr, png->info_ptr);
  png_byte bit_depth = png_get_bit_depth(png->png_ptr, png->info_ptr);
  png_byte color_type = png_get_color_type(png->png_ptr, png->info_ptr);
  png_byte n_channels = png_get_channels(png->png_ptr, png->info_ptr);
  printf("%d %d %d %d %d\n", height, width, bit_depth, color_type, n_channels);

  return 0;
}
