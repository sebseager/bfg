#include <png.h>
#include <stdio.h>
#include <stdlib.h>

/*
// struct bfg_info {
//   char magic[4];
//   uint32_t width;
//   uint32_t height;
// };
*/

typedef struct png_data {
  png_structp png_ptr;
  png_infop info_ptr;
  /*
  // png_uint_32 width;
  // png_uint_32 height;
  // png_byte bit_depth;
  // png_byte color_type;
  // png_byte n_channels;
  */
  png_bytep *row_ptrs;
} * png_data_t;

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
int standardize_png(png_data_t data) {
  png_set_expand(data->png_ptr); // expand PLTE and tRNS tables as needed
  png_read_update_info(data->png_ptr, data->info_ptr); // update info_ptr
  return 0;
}

/*
 * Reads png file at fpath into png_data_t using the libpng API.
 * Caller is responsible for freeing the struct.
 */
int read_png(char *fpath, png_data_t data) {
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

  data->png_ptr =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!data->png_ptr) {
    fprintf(stderr, "png_create_read_struct failed\n");
    fclose(fp);
    return 1;
  }

  data->info_ptr = png_create_info_struct(data->png_ptr);
  if (!data->info_ptr) {
    fprintf(stderr, "png_create_info_struct failed\n");
    png_destroy_read_struct(&data->png_ptr, NULL, NULL);
    fclose(fp);
    return 1;
  }

  png_init_io(data->png_ptr, fp);
  png_set_sig_bytes(data->png_ptr, 8); // we've already read these 8 bytes
  png_read_info(data->png_ptr, data->info_ptr);

  // don't access info_ptr directly
  png_uint_32 height = png_get_image_height(data->png_ptr, data->info_ptr);
  data->row_ptrs = malloc(sizeof(png_bytep) * height);
  for (png_uint_32 i = 0; i < height; i++) {
    data->row_ptrs[i] = malloc(png_get_rowbytes(data->png_ptr, data->info_ptr));
  }

  if (standardize_png(data)) {
    fprintf(stderr, "standardize_png failed\n");
    png_destroy_read_struct(&data->png_ptr, &data->info_ptr, NULL);
    fclose(fp);
    return 1;
  }

  png_read_image(data->png_ptr, data->row_ptrs);

  fclose(fp);
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <png file>\n", argv[0]);
    return 1;
  }

  png_data_t data = malloc(sizeof(struct png_data));
  if (read_png(argv[1], data)) {
    return 1;
  }

  png_uint_32 height = png_get_image_height(data->png_ptr, data->info_ptr);
  png_uint_32 width = png_get_image_width(data->png_ptr, data->info_ptr);
  png_byte bit_depth = png_get_bit_depth(data->png_ptr, data->info_ptr);
  png_byte color_type = png_get_color_type(data->png_ptr, data->info_ptr);
  png_byte n_channels = png_get_channels(data->png_ptr, data->info_ptr);
  printf("%d %d %d %d %d\n", height, width, bit_depth, color_type, n_channels);

  return 0;
}
