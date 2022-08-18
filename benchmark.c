// #include "bfg.h"
#include "convert.h"
#include "util.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <png file>\n", argv[0]);
    return 1;
  }

  clock_t begin;

  struct png_data png;
  struct bfg_raw raw;
  struct bfg_info info;

  if (libpng_read(argv[1], &png)) {
    return 1;
  }

  begin = clock();
  libpng_decode(&png, &raw);
  printf("millis (png decode): %f\n", MILLIS_SINCE(begin));

  begin = clock();
  bfg_img_t img = bfg_encode(&raw, &info);
  bfg_write("bfg_out.bfg", &info, img);
  printf("millis (bfg encode): %f\n", MILLIS_SINCE(begin));

  struct bfg_info info_in;
  struct bfg_raw raw_in;

  bfg_img_t img_in = bfg_read("bfg_out.bfg", &info_in);
  if (!img_in) {
    return 1;
  }

  begin = clock();
  bfg_decode(&info_in, img_in, &raw_in);
  printf("millis (bfg decode): %f\n", MILLIS_SINCE(begin));

  begin = clock();
  libpng_write("bfg_out.png", &raw_in);
  printf("millis (png encode): %f\n", MILLIS_SINCE(begin));

  // DEBUG - converting back to a PNG must give the same size!!
  // it doesn't.... that's not good, let's figure out why
  struct png_data png2;
  libpng_read("bfg_out.png", &png2);

  // print sizes
  unsigned long raw_sz =
      sizeof(struct bfg_raw) + raw.width * raw.height * raw.n_channels;
  fseek(png.fp, 0L, SEEK_END);
  unsigned long png_sz = ftell(png.fp);
  unsigned long bfg_sz = info.n_bytes;
  printf("raw bytes: %lu\n", raw_sz);
  printf("png bytes: %lu (%d%%)\n", png_sz, (int)(100 * png_sz / raw_sz));
  printf("bfg bytes: %lu (%d%%)\n", bfg_sz, (int)(100 * bfg_sz / raw_sz));

  libpng_free(&png);
  bfg_free(&raw, img);
  bfg_free(&raw_in, img_in);

  return 0;
}
