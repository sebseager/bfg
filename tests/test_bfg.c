/*
 * test_bfg.c - Synthetic roundtrip tests for BFG2 encoder/decoder.
 * Compile: gcc -std=c99 -O2 -o test_bfg test_bfg.c bfg.c -I.
 * (no libpng dependency for synthetic tests)
 */

#include "../bfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int tests_run = 0;
static int tests_passed = 0;

/* Create a bfg_raw struct with the given dimensions. Caller must free pixels. */
static struct bfg_raw make_raw(uint32_t w, uint32_t h, uint8_t ch) {
  struct bfg_raw raw;
  raw.width = w;
  raw.height = h;
  raw.n_channels = ch;
  raw.pixels = (uint8_t *)calloc((size_t)w * h * ch, 1);
  return raw;
}

/* Set pixel at (x,y) */
static void set_px(struct bfg_raw *r, uint32_t x, uint32_t y, uint8_t rv,
                   uint8_t gv, uint8_t bv, uint8_t av) {
  uint32_t idx = (y * r->width + x) * r->n_channels;
  r->pixels[idx + 0] = rv;
  r->pixels[idx + 1] = gv;
  r->pixels[idx + 2] = bv;
  if (r->n_channels >= 4) r->pixels[idx + 3] = av;
}

/* Roundtrip test: encode then decode, compare pixels. */
static int roundtrip_test(const char *name, struct bfg_raw *input) {
  tests_run++;
  bfg_header_t header;
  uint32_t enc_len = 0;

  uint8_t *enc = bfg_encode(input, &header, &enc_len);
  if (!enc) {
    printf("  FAIL %s: encode returned NULL\n", name);
    return 0;
  }

  struct bfg_raw output;
  if (bfg_decode(&header, enc, enc_len, &output)) {
    printf("  FAIL %s: decode failed\n", name);
    bfg_free_img(enc);
    return 0;
  }

  if (output.width != input->width || output.height != input->height ||
      output.n_channels != input->n_channels) {
    printf("  FAIL %s: dimension mismatch (%ux%ux%u vs %ux%ux%u)\n", name,
           input->width, input->height, input->n_channels, output.width,
           output.height, output.n_channels);
    bfg_free_raw(&output);
    bfg_free_img(enc);
    return 0;
  }

  uint64_t total = (uint64_t)input->width * input->height * input->n_channels;
  int mismatch = 0;
  for (uint64_t i = 0; i < total; i++) {
    if (input->pixels[i] != output.pixels[i]) {
      uint64_t px = i / input->n_channels;
      uint32_t x = (uint32_t)(px % input->width);
      uint32_t y = (uint32_t)(px / input->width);
      uint32_t c = (uint32_t)(i % input->n_channels);
      printf("  FAIL %s: pixel (%u,%u) ch%u expected %u got %u\n", name, x, y,
             c, input->pixels[i], output.pixels[i]);
      mismatch = 1;
      break;
    }
  }

  double ratio = total > 0 ? 100.0 * enc_len / total : 0;
  if (!mismatch) {
    printf("  PASS %s (%ux%ux%u, %.1f%% ratio, %u bytes)\n", name,
           input->width, input->height, input->n_channels, ratio, enc_len);
    tests_passed++;
  }

  bfg_free_raw(&output);
  bfg_free_img(enc);
  return !mismatch;
}

/* Also test file I/O roundtrip */
static int file_roundtrip_test(const char *name, struct bfg_raw *input) {
  tests_run++;
  bfg_header_t header;
  uint32_t enc_len = 0;

  uint8_t *enc = bfg_encode(input, &header, &enc_len);
  if (!enc) {
    printf("  FAIL %s (file): encode returned NULL\n", name);
    return 0;
  }

  const char *tmp_path = "/tmp/bfg_test_roundtrip.bfg";
  if (bfg_write(tmp_path, &header, enc, enc_len)) {
    printf("  FAIL %s (file): write failed\n", name);
    bfg_free_img(enc);
    return 0;
  }

  bfg_header_t header2;
  uint32_t len2;
  uint8_t *data2 = bfg_read(tmp_path, &header2, &len2);
  if (!data2) {
    printf("  FAIL %s (file): read failed\n", name);
    bfg_free_img(enc);
    return 0;
  }

  struct bfg_raw output;
  if (bfg_decode(&header2, data2, len2, &output)) {
    printf("  FAIL %s (file): decode failed\n", name);
    bfg_free_img(data2);
    bfg_free_img(enc);
    return 0;
  }

  uint64_t total = (uint64_t)input->width * input->height * input->n_channels;
  int ok = (memcmp(input->pixels, output.pixels, (size_t)total) == 0);
  if (ok) {
    printf("  PASS %s (file roundtrip)\n", name);
    tests_passed++;
  } else {
    printf("  FAIL %s (file roundtrip): pixel mismatch\n", name);
  }

  bfg_free_raw(&output);
  bfg_free_img(data2);
  bfg_free_img(enc);
  remove(tmp_path);
  return ok;
}

/* ---- test cases ---- */

static void test_solid_black(void) {
  struct bfg_raw r = make_raw(64, 64, 3);
  /* already all zeros = black */
  roundtrip_test("solid_black_rgb", &r);
  free(r.pixels);
}

static void test_solid_white(void) {
  struct bfg_raw r = make_raw(64, 64, 4);
  memset(r.pixels, 255, (size_t)64 * 64 * 4);
  roundtrip_test("solid_white_rgba", &r);
  free(r.pixels);
}

static void test_solid_color(void) {
  struct bfg_raw r = make_raw(100, 100, 3);
  for (uint32_t i = 0; i < 100 * 100; i++) {
    r.pixels[i * 3 + 0] = 0x42;
    r.pixels[i * 3 + 1] = 0x8A;
    r.pixels[i * 3 + 2] = 0xCD;
  }
  roundtrip_test("solid_color_rgb", &r);
  free(r.pixels);
}

static void test_horizontal_gradient(void) {
  struct bfg_raw r = make_raw(256, 64, 3);
  for (uint32_t y = 0; y < 64; y++) {
    for (uint32_t x = 0; x < 256; x++) {
      uint32_t idx = (y * 256 + x) * 3;
      r.pixels[idx + 0] = (uint8_t)x;
      r.pixels[idx + 1] = (uint8_t)x;
      r.pixels[idx + 2] = (uint8_t)x;
    }
  }
  roundtrip_test("h_gradient_rgb", &r);
  free(r.pixels);
}

static void test_vertical_gradient(void) {
  struct bfg_raw r = make_raw(64, 256, 4);
  for (uint32_t y = 0; y < 256; y++) {
    for (uint32_t x = 0; x < 64; x++) {
      set_px(&r, x, y, (uint8_t)y, (uint8_t)(255 - y), (uint8_t)(y / 2), 255);
    }
  }
  roundtrip_test("v_gradient_rgba", &r);
  free(r.pixels);
}

static void test_checkerboard(void) {
  struct bfg_raw r = make_raw(128, 128, 3);
  for (uint32_t y = 0; y < 128; y++) {
    for (uint32_t x = 0; x < 128; x++) {
      uint8_t v = ((x + y) & 1) ? 255 : 0;
      uint32_t idx = (y * 128 + x) * 3;
      r.pixels[idx + 0] = v;
      r.pixels[idx + 1] = v;
      r.pixels[idx + 2] = v;
    }
  }
  roundtrip_test("checkerboard_rgb", &r);
  free(r.pixels);
}

static void test_random_noise(void) {
  struct bfg_raw r = make_raw(200, 200, 4);
  srand(12345);
  for (uint32_t i = 0; i < 200 * 200 * 4; i++) {
    r.pixels[i] = (uint8_t)(rand() & 0xFF);
  }
  roundtrip_test("random_noise_rgba", &r);
  free(r.pixels);
}

static void test_1x1(void) {
  struct bfg_raw r = make_raw(1, 1, 3);
  r.pixels[0] = 42; r.pixels[1] = 100; r.pixels[2] = 200;
  roundtrip_test("1x1_rgb", &r);
  file_roundtrip_test("1x1_rgb", &r);
  free(r.pixels);
}

static void test_1_wide(void) {
  struct bfg_raw r = make_raw(1, 100, 3);
  for (uint32_t y = 0; y < 100; y++) {
    r.pixels[y * 3 + 0] = (uint8_t)(y * 2);
    r.pixels[y * 3 + 1] = (uint8_t)(y * 3);
    r.pixels[y * 3 + 2] = (uint8_t)(y);
  }
  roundtrip_test("1_wide_rgb", &r);
  free(r.pixels);
}

static void test_1_tall(void) {
  struct bfg_raw r = make_raw(100, 1, 4);
  for (uint32_t x = 0; x < 100; x++) {
    set_px(&r, x, 0, (uint8_t)(x * 2), (uint8_t)x, (uint8_t)(255 - x), 200);
  }
  roundtrip_test("1_tall_rgba", &r);
  free(r.pixels);
}

static void test_alpha_variation(void) {
  struct bfg_raw r = make_raw(64, 64, 4);
  for (uint32_t y = 0; y < 64; y++) {
    for (uint32_t x = 0; x < 64; x++) {
      set_px(&r, x, y, (uint8_t)(x * 4), (uint8_t)(y * 4),
             (uint8_t)((x + y) * 2), (uint8_t)(x * 4));
    }
  }
  roundtrip_test("alpha_variation_rgba", &r);
  free(r.pixels);
}

static void test_stripes(void) {
  struct bfg_raw r = make_raw(256, 128, 3);
  for (uint32_t y = 0; y < 128; y++) {
    for (uint32_t x = 0; x < 256; x++) {
      uint8_t v = (y % 8 < 4) ? 200 : 50;
      uint32_t idx = (y * 256 + x) * 3;
      r.pixels[idx + 0] = v;
      r.pixels[idx + 1] = (uint8_t)(255 - v);
      r.pixels[idx + 2] = 128;
    }
  }
  roundtrip_test("stripes_rgb", &r);
  free(r.pixels);
}

static void test_large(void) {
  struct bfg_raw r = make_raw(1920, 1080, 3);
  srand(99999);
  /* natural-ish content: smooth patches with noise */
  for (uint32_t y = 0; y < 1080; y++) {
    for (uint32_t x = 0; x < 1920; x++) {
      uint32_t idx = (y * 1920 + x) * 3;
      uint8_t base = (uint8_t)((x / 32 + y / 32) * 7);
      r.pixels[idx + 0] = (uint8_t)(base + (rand() % 8));
      r.pixels[idx + 1] = (uint8_t)(base + 30 + (rand() % 8));
      r.pixels[idx + 2] = (uint8_t)(base + 60 + (rand() % 8));
    }
  }
  roundtrip_test("large_1920x1080_rgb", &r);
  free(r.pixels);
}

static void test_file_io(void) {
  struct bfg_raw r = make_raw(32, 32, 4);
  for (uint32_t i = 0; i < 32 * 32 * 4; i++) {
    r.pixels[i] = (uint8_t)(i & 0xFF);
  }
  file_roundtrip_test("file_io_rgba", &r);
  free(r.pixels);
}

int main(void) {
  printf("BFG2 synthetic roundtrip tests\n");
  printf("==============================\n\n");

  test_solid_black();
  test_solid_white();
  test_solid_color();
  test_horizontal_gradient();
  test_vertical_gradient();
  test_checkerboard();
  test_random_noise();
  test_1x1();
  test_1_wide();
  test_1_tall();
  test_alpha_variation();
  test_stripes();
  test_large();
  test_file_io();

  printf("\n%d / %d tests passed\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}
