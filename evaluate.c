#include "convert.h"
#include "util.h"
#include <libgen.h>
#include <stdlib.h>
#include <string.h>

#define FILENAME_LEN (14)

struct stats {
  uint32_t raw_bytes;
  uint32_t png_bytes;
  uint32_t bfg_bytes;
  double png_enc_millis;
  double bfg_enc_millis;
  double png_dec_millis;
  double bfg_dec_millis;
  char name[FILENAME_LEN + 1];
};

void set_name_stat(char *fname, struct stats *s) {
  char *base = basename(fname);
  for (int i = 0; i < FILENAME_LEN; i++) {
    if (base[i] == '\0') {
      // write spaces in case name is short
      memset(&s->name[i], ' ', FILENAME_LEN);
      break;
    }
    s->name[i] = base[i];
  }
  s->name[FILENAME_LEN] = '\0';
}

void print_stats(struct stats *stats, unsigned int n_img) {
  printf("\t\tpng\t\t\tbfg\n");
  printf("image filename\tratio\tenc ms\tdec ms\tratio\tenc ms"
         "\tdec ms\n");
  printf("--------------\t-----\t------\t------\t-----\t------\t------\n");
  for (unsigned int i = 0; i < n_img; i++) {
    struct stats s = stats[i];
    char *name = s.name;
    const unsigned int png_ratio = (100 * s.png_bytes / s.raw_bytes);
    const unsigned int bfg_ratio = (100 * s.bfg_bytes / s.raw_bytes);
    printf("%s\t%d%%\t%.4g\t%.4g\t%d%%\t%.4g\t%.4g\n", name, png_ratio,
           s.png_enc_millis, s.png_dec_millis, bfg_ratio, s.bfg_enc_millis,
           s.bfg_dec_millis);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <png files>\n", argv[0]);
    return 1;
  }

  const unsigned int n_img = argc - 1;
  struct stats stats_arr[n_img];
  clock_t begin;

  for (unsigned int i = 0; i < n_img; i++) {
    struct png_data png;
    struct bfg_raw raw;
    struct bfg_info info;

    if (libpng_read(argv[i + 1], &png)) {
      return 1;
    }

    begin = clock();
    if (libpng_decode(&png, &raw)) {
      return 1;
    }
    stats_arr[i].png_dec_millis = MILLIS_SINCE(begin);

    begin = clock();
    bfg_img_t img = bfg_encode(&raw, &info);
    stats_arr[i].bfg_enc_millis = MILLIS_SINCE(begin);
    if (!img) {
      return 1;
    }

    bfg_write("bfg_out.bfg", &info, img);

    struct bfg_info info_in;
    struct bfg_raw raw_in;

    bfg_img_t img_in = bfg_read("bfg_out.bfg", &info_in);
    if (!img_in) {
      return 1;
    }

    begin = clock();
    bfg_decode(&info_in, img_in, &raw_in);
    stats_arr[i].bfg_dec_millis = MILLIS_SINCE(begin);

    begin = clock();
    libpng_write("bfg_out.png", &raw_in);
    stats_arr[i].png_enc_millis = MILLIS_SINCE(begin);

    // write stats
    set_name_stat(argv[i + 1], &stats_arr[i]);
    stats_arr[i].raw_bytes =
        sizeof(struct bfg_raw) + raw.width * raw.height * raw.n_channels;
    fseek(png.fp, 0L, SEEK_END);
    stats_arr[i].png_bytes = ftell(png.fp);
    stats_arr[i].bfg_bytes = info.n_bytes;

    // cleanup
    libpng_free(&png);
    bfg_free(&raw, img);
    bfg_free(&raw_in, img_in);
  }

  print_stats(stats_arr, n_img);
  return 0;
}
