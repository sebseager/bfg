#include "convert.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

void libpng_free(png_data_t png) {
  if (!png) return;

  FCLOSE(png->fp);

  if (png->png_ptr && png->info_ptr && png->row_ptrs) {
    png_uint_32 height = png_get_image_height(png->png_ptr, png->info_ptr);
    for (png_uint_32 y = 0; y < height; y++) {
      if (png->row_ptrs[y]) free(png->row_ptrs[y]);
    }
    free(png->row_ptrs);
    png->row_ptrs = NULL;
  }

  if (png->png_ptr) {
    if (png->file_mode == 'w')
      png_destroy_write_struct(&png->png_ptr, &png->info_ptr);
    else if (png->file_mode == 'r')
      png_destroy_read_struct(&png->png_ptr, &png->info_ptr, NULL);
  }
}

int libpng_read(char *fpath, png_data_t png) {
  if (!fpath || !png) return 1;

  memset(png, 0, sizeof(*png));
  png->file_mode = 'r';
  png->fp = fopen(fpath, "rb");
  if (!png->fp) return 1;

  png_byte sig[8];
  if (fread(sig, 1, 8, png->fp) != 8 || png_sig_cmp(sig, 0, 8)) {
    fprintf(stderr, "Not a valid png file\n");
    libpng_free(png);
    return 1;
  }

  png->png_ptr =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png->png_ptr) { libpng_free(png); return 1; }

  png->info_ptr = png_create_info_struct(png->png_ptr);
  if (!png->info_ptr) { libpng_free(png); return 1; }

  png_init_io(png->png_ptr, png->fp);
  png_set_sig_bytes(png->png_ptr, 8);
  png_read_info(png->png_ptr, png->info_ptr);

  /* expand palette and tRNS to full RGBA */
  png_set_expand(png->png_ptr);
  /* convert 16-bit to 8-bit */
  png_set_strip_16(png->png_ptr);
  /* unpack sub-byte depths */
  png_set_packing(png->png_ptr);

  /* expand grayscale to RGB so BFG always gets 3 or 4 channels */
  png_byte color_type = png_get_color_type(png->png_ptr, png->info_ptr);
  if (color_type == PNG_COLOR_TYPE_GRAY ||
      color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(png->png_ptr);
  }

  png_read_update_info(png->png_ptr, png->info_ptr);

  png_uint_32 height = png_get_image_height(png->png_ptr, png->info_ptr);
  png_uint_32 row_bytes = png_get_rowbytes(png->png_ptr, png->info_ptr);
  png->row_ptrs = malloc(height * sizeof(png_bytep));
  for (png_uint_32 y = 0; y < height; y++) {
    png->row_ptrs[y] = malloc(row_bytes * sizeof(png_byte));
  }

  png_read_image(png->png_ptr, png->row_ptrs);
  png_read_end(png->png_ptr, png->info_ptr);

  return 0;
}

int libpng_decode(png_data_t png, bfg_raw_t raw) {
  if (!png || !raw) return 1;

  raw->width = png_get_image_width(png->png_ptr, png->info_ptr);
  raw->height = png_get_image_height(png->png_ptr, png->info_ptr);
  raw->n_channels = png_get_channels(png->png_ptr, png->info_ptr);

  /* after gray_to_rgb, channels should be 3 or 4 */
  if (raw->n_channels < 3 || raw->n_channels > 4) return 1;

  uint64_t total_bytes = (uint64_t)raw->width * raw->height * raw->n_channels;
  if (total_bytes > UINT32_MAX) return 1;

  raw->pixels = malloc((size_t)total_bytes);
  if (!raw->pixels) return 1;

  png_uint_32 row_bytes = png_get_rowbytes(png->png_ptr, png->info_ptr);
  for (png_uint_32 y = 0; y < raw->height; y++) {
    memcpy(raw->pixels + y * row_bytes, png->row_ptrs[y], row_bytes);
  }

  return 0;
}

int libpng_write(char *fpath, bfg_raw_t raw) {
  struct png_data png;
  if (!fpath || !raw) return 1;

  png.row_ptrs = NULL;
  png.file_mode = 'w';
  png.fp = fopen(fpath, "wb");
  if (!png.fp) return 1;

  png.png_ptr =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png.png_ptr) return 1;

  png.info_ptr = png_create_info_struct(png.png_ptr);
  if (!png.info_ptr) return 1;

  png_init_io(png.png_ptr, png.fp);

  png_byte color_type;
  switch (raw->n_channels) {
  case 3: color_type = PNG_COLOR_TYPE_RGB; break;
  case 4: color_type = PNG_COLOR_TYPE_RGB_ALPHA; break;
  default:
    fprintf(stderr, "Image with %d channels not supported\n", raw->n_channels);
    return 1;
  }

  png_set_IHDR(png.png_ptr, png.info_ptr, raw->width, raw->height, 8,
               color_type, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png.png_ptr, png.info_ptr);

  png_uint_32 row_bytes = raw->width * raw->n_channels;
  for (png_uint_32 y = 0; y < raw->height; y++) {
    png_write_row(png.png_ptr, raw->pixels + FLAT_INDEX(0, y, row_bytes));
  }

  png_write_end(png.png_ptr, NULL);
  libpng_free(&png);

  return 0;
}
