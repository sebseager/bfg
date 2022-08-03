#include <png.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct png_data {
  png_structp png_ptr;
  png_infop info_ptr;
  png_bytep *row_ptrs;
} * png_data_t;

typedef enum { GRAY, GRAY_ALPHA, RGB, RGB_ALPHA } bfg_color_type_t;

/*
 * Representation in memory of a decoded BFG image.
 */
typedef struct bfg_data {
  uint32_t width;
  uint32_t height;
  uint8_t bit_depth;
  uint8_t n_channels;
  bfg_color_type_t color_type;
  uint8_t palette_len;
  uint8_t *pixels;
  uint8_t *palette;
} * bfg_data_t;

/*
 * Standardize the row_ptrs array in png_data_t to contain all available color
 * and alpha information, expanding any references to header tables.
 *
 * libpng specifies several color_type options:
 *   0  PNG_COLOR_TYPE_GRAY         1 channel
 *   2  PNG_COLOR_TYPE_RGB          3 channels
 *   3  PNG_COLOR_TYPE_PALETTE      1 channel
 *   4  PNG_COLOR_TYPE_GRAY_ALPHA   2 channels
 *   6  PNG_COLOR_TYPE_RGB_ALPHA    4 channels
 *
 * For PNG_COLOR_TYPE_GRAY the file header may contain a tRNS chunk containing a
 * single two-byte gray level value between 0 to (2^bitdepth)-1. If bitdepth <
 * 16, then least significant bits are filled first. All pixel values matching
 * this gray level are transparent (alpha = 0); all others are opaque (alpha =
 * (2^bitdepth)-1).
 *
 * For PNG_COLOR_TYPE_RGB the file header may contain a six-byte tRNS chunk (one
 * word for each R, G, B) to encode a color level. As with PNG_COLOR_TYPE_GRAY,
 * each channel value ranges from 0 to (2^bitdepth)-1, and is stored at the
 * least significant end of its respective word.
 *
 * For PNG_COLOR_TYPE_PALETTE, each image pixel is a single byte index into a
 * PLTE table at the beginning of the file. Each PLTE entry is fixed at 3 bytes
 * (one per 8-bit RGB channel) no matter the bit depth, and there can be at most
 * 256 entries. To encode transparency, the file may also contain a tRNS table
 * with up to 256 1-byte entries (0 to 255), each encoding an alpha value for
 * the corresponding palette entry. If there are fewer tRNS entries than PLTE
 * entries, remaining alpha values are assumed to be 255 (opaque).
 *
 * I'll be honest, I was just about to implement the transformations from PLTE
 * and tRNS chunks to raw color and/or alpha values from scratch before
 * realizing png_set_expand() existed. RTFM.
 *
 * After standardize_png, color_type will be either 0, 2, 4, or 6 with 1, 3, 2,
 * and 4 channels respectively.
 */
int libpng_standardize(png_data_t png) {
  png_set_expand(png->png_ptr); // expand PLTE and tRNS tables as needed
  png_read_update_info(png->png_ptr, png->info_ptr); // update info_ptr
  return 0;
}

/*
 * Reads png file at fpath into png_data_t using the libpng API.
 * Caller is responsible for freeing the struct with libpng_free.
 */
int libpng_read(char *fpath, png_data_t png) {
  FILE *fp = fopen(fpath, "rb");
  if (!fp) {
    fprintf(stderr, "Could not open file %s\n", fpath);
    return 1;
  }

  // verify png signature in first 8 bytes
  png_byte sig[8];
  fread(sig, 1, 8, fp);
  if (png_sig_cmp(sig, 0, 8)) {
    fprintf(stderr, "Not a valid png file\n");
    fclose(fp);
    return 1;
  }

  png->png_ptr =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png->png_ptr) {
    fprintf(stderr, "png_create_read_struct failed\n");
    fclose(fp);
    return 1;
  }

  png->info_ptr = png_create_info_struct(png->png_ptr);
  if (!png->info_ptr) {
    fprintf(stderr, "png_create_info_struct failed\n");
    png_destroy_read_struct(&png->png_ptr, NULL, NULL);
    fclose(fp);
    return 1;
  }

  png_init_io(png->png_ptr, fp);
  png_set_sig_bytes(png->png_ptr, 8); // we've already read these 8 bytes
  png_read_info(png->png_ptr, png->info_ptr);

  // don't access info_ptr directly
  png_uint_32 height = png_get_image_height(png->png_ptr, png->info_ptr);
  png->row_ptrs = malloc(sizeof(png_bytep) * height);
  for (png_uint_32 i = 0; i < height; i++) {
    png->row_ptrs[i] = malloc(png_get_rowbytes(png->png_ptr, png->info_ptr));
  }

  if (libpng_standardize(png)) {
    fprintf(stderr, "standardize_png failed\n");
    png_destroy_read_struct(&png->png_ptr, &png->info_ptr, NULL);
    fclose(fp);
    return 1;
  }

  png_read_image(png->png_ptr, png->row_ptrs);

  fclose(fp);
  return 0;
}

/*
 * Allocates and populates bfg data struct with data from libpng data struct.
 * Returns NULL on failure.
 */
bfg_data_t libpng_decode(png_data_t png) { return NULL; }

/*
 * Allocates and populates libpng data struct with data from bfg data struct.
 * Returns NULL on failure.
 */
png_data_t libpng_encode(bfg_data_t bfg) { return NULL; }

/*
 * Writes new png file at fpath from given libpng data struct.
 * Returns 0 on success, 1 on failure.
 */
int libpng_write(char *fpath, png_data_t png) { return 0; }

/*
 * Frees everything allocated within the data struct by libpng.
 */
void libpng_free(png_data_t png) {
  png_free_data(png->png_ptr, png->info_ptr, PNG_FREE_ALL, -1);
}

// TODO: BFG FUNCTIONS START HERE



int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <png file>\n", argv[0]);
    return 1;
  }

  png_data_t png = malloc(sizeof(struct png_data));
  if (libpng_read(argv[1], png)) {
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
