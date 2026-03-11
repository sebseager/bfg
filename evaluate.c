#include "convert.h"
#include "util.h"
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define FILENAME_LEN (30)

struct stats {
  uint32_t raw_bytes;
  uint32_t png_bytes;
  uint32_t bfg_bytes;
  double png_enc_millis;
  double bfg_enc_millis;
  double png_dec_millis;
  double bfg_dec_millis;
  int verified; /* 1 = pixel-perfect roundtrip, 0 = mismatch, -1 = skipped */
  char name[FILENAME_LEN + 1];
};

void print_stats(struct stats *stats, unsigned int n_img) {
  printf("\t\t\t\tpng\t\t\tbfg\n");
  printf("%-*s\tratio\tenc ms\tdec ms\tratio\tenc ms\tdec ms\tverify\n",
         FILENAME_LEN, "image");
  for (int i = 0; i < FILENAME_LEN; i++) putchar('-');
  printf("\t-----\t------\t------\t-----\t------\t------\t------\n");

  for (unsigned int i = 0; i < n_img; i++) {
    struct stats s = stats[i];
    double png_ratio =
        s.raw_bytes ? (100.0 * s.png_bytes / s.raw_bytes) : 9999;
    double bfg_ratio =
        s.raw_bytes ? (100.0 * s.bfg_bytes / s.raw_bytes) : 9999;
    const char *vstr =
        s.verified == 1 ? "PASS" : (s.verified == 0 ? "FAIL" : "SKIP");
    printf("%-*s\t%.1f%%\t%.2f\t%.2f\t%.1f%%\t%.2f\t%.2f\t%s\n",
           FILENAME_LEN, s.name, png_ratio, s.png_enc_millis, s.png_dec_millis,
           bfg_ratio, s.bfg_enc_millis, s.bfg_dec_millis, vstr);
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
  int any_fail = 0;

  for (unsigned int i = 0; i < n_img; i++) {
    struct png_data png;
    struct bfg_raw raw;
    bfg_header_t header;
    uint32_t bfg_len = 0;

    stats_arr[i].verified = -1; /* default: skipped */

    /* write file basename to stats struct */
    char *base = basename(argv[i + 1]);
    memset(stats_arr[i].name, 0, sizeof(stats_arr[i].name));
    strncpy(stats_arr[i].name, base, FILENAME_LEN);

    if (libpng_read(argv[i + 1], &png)) {
      fprintf(stderr, "Could not open file %s\n", argv[i + 1]);
      continue;
    }

    begin = clock();
    if (libpng_decode(&png, &raw)) {
      fprintf(stderr, "Could not decode file %s\n", argv[i + 1]);
      libpng_free(&png);
      continue;
    }
    stats_arr[i].png_dec_millis = MILLIS_SINCE(begin);

    /* encode to BFG */
    begin = clock();
    bfg_img_t img = bfg_encode(&raw, &header, &bfg_len);
    stats_arr[i].bfg_enc_millis = MILLIS_SINCE(begin);
    if (!img) {
      fprintf(stderr, "Could not encode file %s\n", argv[i + 1]);
      bfg_free_raw(&raw);
      libpng_free(&png);
      continue;
    }

    /* write BFG file */
    char out_path[strlen(base) + 16];
    strcpy(out_path, "output/");
    mkdir(out_path, 0777);
    strcat(out_path, base);
    strcat(out_path, ".bfg");
    if (bfg_write(out_path, &header, img, bfg_len)) {
      fprintf(stderr, "Could not write file %s\n", out_path);
      bfg_free_img(img);
      bfg_free_raw(&raw);
      libpng_free(&png);
      continue;
    }

    /* read back and decode */
    bfg_header_t header_in;
    uint32_t data_in_len = 0;
    uint8_t *data_in = bfg_read(out_path, &header_in, &data_in_len);
    if (!data_in) {
      fprintf(stderr, "Could not read file %s\n", out_path);
      bfg_free_img(img);
      bfg_free_raw(&raw);
      libpng_free(&png);
      continue;
    }

    struct bfg_raw raw_in;
    begin = clock();
    if (bfg_decode(&header_in, data_in, data_in_len, &raw_in)) {
      fprintf(stderr, "Could not decode BFG %s\n", out_path);
      bfg_free_img(data_in);
      bfg_free_img(img);
      bfg_free_raw(&raw);
      libpng_free(&png);
      continue;
    }
    stats_arr[i].bfg_dec_millis = MILLIS_SINCE(begin);

    /* pixel-perfect verification */
    int verified = 1;
    if (raw.width == raw_in.width && raw.height == raw_in.height &&
        raw.n_channels == raw_in.n_channels) {
      uint64_t total = (uint64_t)raw.width * raw.height * raw.n_channels;
      if (memcmp(raw.pixels, raw_in.pixels, (size_t)total) != 0) {
        verified = 0;
        any_fail = 1;
        /* find first mismatch for debugging */
        for (uint64_t j = 0; j < total; j++) {
          if (raw.pixels[j] != raw_in.pixels[j]) {
            uint64_t px_idx = j / raw.n_channels;
            uint32_t px_x = (uint32_t)(px_idx % raw.width);
            uint32_t px_y = (uint32_t)(px_idx / raw.width);
            uint32_t px_c = (uint32_t)(j % raw.n_channels);
            fprintf(stderr,
                    "  MISMATCH %s: pixel (%u,%u) ch%u: expected %u got %u\n",
                    base, px_x, px_y, px_c, raw.pixels[j], raw_in.pixels[j]);
            break;
          }
        }
      }
    } else {
      verified = 0;
      any_fail = 1;
      fprintf(stderr, "  MISMATCH %s: dimensions differ\n", base);
    }
    stats_arr[i].verified = verified;

    /* write decoded result as PNG for visual inspection */
    strcat(out_path, ".png");
    begin = clock();
    if (libpng_write(out_path, &raw_in)) {
      fprintf(stderr, "Could not write file %s\n", out_path);
    }
    stats_arr[i].png_enc_millis = MILLIS_SINCE(begin);

    /* stats */
    stats_arr[i].raw_bytes = raw.width * raw.height * raw.n_channels;
    fseek(png.fp, 0L, SEEK_END);
    stats_arr[i].png_bytes = ftell(png.fp);
    stats_arr[i].bfg_bytes = bfg_len + BFG_HEADER_SIZE;

    /* cleanup */
    bfg_free_raw(&raw_in);
    bfg_free_img(data_in);
    bfg_free_img(img);
    bfg_free_raw(&raw);
    libpng_free(&png);
  }

  print_stats(stats_arr, n_img);
  free(stats_arr);

  if (any_fail) {
    fprintf(stderr, "\nSome images FAILED pixel-perfect verification!\n");
    return 1;
  }
  return 0;
}
