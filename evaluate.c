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

void print_stats(struct stats *stats, unsigned int n_img) {
  printf("\t\tpng\t\t\tbfg\n");
  printf("image filename\tratio\tenc ms\tdec ms\tratio\tenc ms"
         "\tdec ms\n");
  printf("--------------\t-----\t------\t------\t-----\t------\t------\n");
  for (unsigned int i = 0; i < n_img; i++) {
    struct stats s = stats[i];
    int png_ratio = s.raw_bytes ? (100 * s.png_bytes / s.raw_bytes) : 9999;
    int bfg_ratio = s.raw_bytes ? (100 * s.bfg_bytes / s.raw_bytes) : 9999;
    printf("%s\t%d%%\t%.4g\t%.4g\t%d%%\t%.4g\t%.4g\n", s.name, png_ratio,
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

  struct stats *stats_arr = malloc(sizeof(struct stats) * n_img);
  clock_t begin;

  for (unsigned int i = 0; i < n_img; i++) {
    struct png_data png;
    struct bfg_raw raw;
    struct bfg_info info;

    // write file basename to stats struct
    char *base = basename(argv[i + 1]);
    memset(&stats_arr[i].name, ' ', FILENAME_LEN);
    stats_arr[i].name[FILENAME_LEN] = '\0';
    strncpy(stats_arr[i].name, base, FILENAME_LEN);

    if (libpng_read(argv[i + 1], &png)) {
      fprintf(stderr, "Could not open file %s\n", argv[i + 1]);
      continue;
    }

    begin = clock();
    if (libpng_decode(&png, &raw)) {
      fprintf(stderr, "Could not decode file %s\n", argv[i + 1]);
      continue;
    }
    stats_arr[i].png_dec_millis = MILLIS_SINCE(begin);

    begin = clock();
    bfg_img_t img = bfg_encode(&raw, &info);
    stats_arr[i].bfg_enc_millis = MILLIS_SINCE(begin);
    if (!img) {
      fprintf(stderr, "Could not encode file %s\n", argv[i + 1]);
      continue;
    }

    // construct output path by appending .bfg
    char out_path[strlen(base) + 16];
    strcpy(out_path, "output/");
    strcat(out_path, base);
    strcat(out_path, ".bfg");
    if (bfg_write(out_path, &info, img)) {
      fprintf(stderr, "Could not write file %s\n", out_path);
      continue;
    }

    struct bfg_info info_in;
    struct bfg_raw raw_in;

    bfg_img_t img_in = bfg_read(out_path, &info_in);
    if (!img_in) {
      fprintf(stderr, "Could not read file %s\n", out_path);
      continue;
    }

    begin = clock();
    if (bfg_decode(&info_in, img_in, &raw_in)) {
      continue;
    }
    stats_arr[i].bfg_dec_millis = MILLIS_SINCE(begin);

    //
    strcat(out_path, ".png");
    begin = clock();
    if (libpng_write(out_path, &raw_in)) {
      fprintf(stderr, "Could not write file %s\n", out_path);
      continue;
    }
    stats_arr[i].png_enc_millis = MILLIS_SINCE(begin);

    // write stats
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

  free(stats_arr);
  return 0;
}
