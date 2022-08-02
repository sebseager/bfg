#include <png.h>
#include <stdio.h>
#include <stdlib.h>

// struct bfg_info {
//   char magic[4];
//   uint32_t width;
//   uint32_t height;
// };

typedef struct png_data {
  png_structp png_ptr;
  png_infop info_ptr;
  png_uint_32 width;
  png_uint_32 height;
  png_bytep *row_ptrs;
} * png_data_t;

int read_png(char *fname, png_data_t data) {
  FILE *fp = fopen(fname, "rb");
  if (!fp) {
    fprintf(stderr, "Could not open file %s\n", fname);
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
  png_read_info(data->png_ptr, data->info_ptr);
  data->width = png_get_image_width(data->png_ptr, data->info_ptr);
  data->height = png_get_image_height(data->png_ptr, data->info_ptr);

  data->row_ptrs = malloc(sizeof(png_bytep) * data->height);
  for (png_uint_32 i = 0; i < data->height; i++) {
    data->row_ptrs[i] = malloc(png_get_rowbytes(data->png_ptr, data->info_ptr));
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

  printf("%d %d\n", data->width, data->height);
  return 0;
}
