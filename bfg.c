#include "bfg.h"
#include <stdio.h>
#include <string.h>

/* ---- helpers ---- */

static inline bfg_pixel_t bfg_read_pixel(const uint8_t *px, uint8_t ch) {
  bfg_pixel_t p;
  p.r = px[0];
  p.g = px[1];
  p.b = px[2];
  p.a = (ch >= 4) ? px[3] : 255;
  return p;
}

static inline void bfg_write_pixel(uint8_t *px, bfg_pixel_t p, uint8_t ch) {
  px[0] = p.r;
  px[1] = p.g;
  px[2] = p.b;
  if (ch >= 4) px[3] = p.a;
}

static inline int bfg_pixel_eq(bfg_pixel_t a, bfg_pixel_t b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static inline uint32_t bfg_hash(bfg_pixel_t p) {
  return (p.r * 3u ^ p.g * 5u ^ p.b * 11u ^ p.a * 7u) & (BFG_CACHE_SIZE - 1);
}

static inline bfg_pixel_t bfg_predict(bfg_pixel_t left, bfg_pixel_t above) {
  bfg_pixel_t p;
  p.r = ((uint16_t)left.r + above.r) >> 1;
  p.g = ((uint16_t)left.g + above.g) >> 1;
  p.b = ((uint16_t)left.b + above.b) >> 1;
  p.a = ((uint16_t)left.a + above.a) >> 1;
  return p;
}

/* ---- encoder ---- */

bfg_img_t bfg_encode(bfg_raw_t raw, bfg_header_t *header, uint32_t *out_len) {
  if (!raw || !header || !out_len) return NULL;
  if (!raw->width || !raw->height || !raw->n_channels) return NULL;
  if (raw->n_channels < 3 || raw->n_channels > 4) return NULL;

  uint32_t w = raw->width;
  uint32_t h = raw->height;
  uint8_t ch = raw->n_channels;
  uint64_t n_px = (uint64_t)w * h;
  if (n_px > BFG_MAX_PIXELS) return NULL;

  /* fill header */
  header->magic = BFG_MAGIC;
  header->width = w;
  header->height = h;
  header->channels = ch;
  memset(header->reserved, 0, 3);

  /* worst case: every pixel is RGBA literal (5 bytes each) + padding */
  uint64_t max_size = n_px * 5 + 16;
  if (max_size > UINT32_MAX) return NULL;
  uint8_t *out = (uint8_t *)BFG_MALLOC((size_t)max_size);
  if (!out) return NULL;

  bfg_pixel_t cache[BFG_CACHE_SIZE];
  memset(cache, 0, sizeof(cache));

  /* prev_row stores the previous row's pixels for 2D prediction */
  bfg_pixel_t *prev_row = (bfg_pixel_t *)BFG_MALLOC(w * sizeof(bfg_pixel_t));
  if (!prev_row) { BFG_FREE(out); return NULL; }

  /* initialize prev_row to default prediction origin */
  for (uint32_t x = 0; x < w; x++) {
    prev_row[x].r = 0; prev_row[x].g = 0;
    prev_row[x].b = 0; prev_row[x].a = 255;
  }

  bfg_pixel_t prev = {0, 0, 0, 255};
  uint32_t run = 0;
  uint32_t p = 0; /* write position in output */

  for (uint32_t y = 0; y < h; y++) {
    bfg_pixel_t left = {0, 0, 0, 255};
    for (uint32_t x = 0; x < w; x++) {
      uint32_t idx = (y * w + x) * ch;
      bfg_pixel_t px = bfg_read_pixel(&raw->pixels[idx], ch);

      /* compute 2D prediction */
      bfg_pixel_t above = prev_row[x];
      bfg_pixel_t pred;
      if (y == 0) {
        pred = left; /* first row: predict from left */
      } else if (x == 0) {
        pred = above; /* first col: predict from above */
      } else {
        pred = bfg_predict(left, above);
      }

      /* RUN check */
      if (bfg_pixel_eq(px, prev)) {
        run++;
        if (run == 288) {
          /* flush max extended run: 32 (RUN) + 256 (RUN2) */
          out[p++] = BFG_OP_RUN | 31;  /* run of 32 */
          out[p++] = BFG_OP_RUN2;
          out[p++] = 255;               /* run of 256 */
          run = 0;
        }
        prev_row[x] = px;
        left = px;
        continue;
      }

      /* flush pending run before encoding a different pixel */
      if (run > 0) {
        if (run <= 32) {
          out[p++] = BFG_OP_RUN | (run - 1);
        } else {
          out[p++] = BFG_OP_RUN | 31;           /* first 32 */
          out[p++] = BFG_OP_RUN2;
          out[p++] = (uint8_t)(run - 33);        /* remaining 1..256 */
        }
        run = 0;
      }

      /* compute luma-correlated residuals from prediction */
      int dg = (int)px.g - (int)pred.g;
      int dr = (int)px.r - (int)pred.r;
      int db = (int)px.b - (int)pred.b;

      /* wrap to signed byte range */
      if (dg > 127) dg -= 256;
      if (dg < -128) dg += 256;
      if (dr > 127) dr -= 256;
      if (dr < -128) dr += 256;
      if (db > 127) db -= 256;
      if (db < -128) db += 256;

      /* luma-correlated: encode r and b as offsets from green delta */
      int dr_dg = dr - dg;
      int db_dg = db - dg;

      /* DELTA1: dg in [-4..3], (dr-dg) in [-2..1], (db-dg) in [-2..1] */
      if (px.a == prev.a &&
          dg >= -4 && dg <= 3 &&
          dr_dg >= -2 && dr_dg <= 1 &&
          db_dg >= -2 && db_dg <= 1) {
        out[p++] = (uint8_t)(((dg + 4) << 4) | ((dr_dg + 2) << 2) | (db_dg + 2));
      }
      /* CACHE */
      else if (bfg_pixel_eq(cache[bfg_hash(px)], px)) {
        out[p++] = BFG_OP_CACHE | (bfg_hash(px) & 0x0F);
      }
      /* DELTA2: dg in [-32..31], (dr-dg) in [-8..7], (db-dg) in [-8..7] */
      else if (px.a == prev.a &&
               dg >= -32 && dg <= 31 &&
               dr_dg >= -8 && dr_dg <= 7 &&
               db_dg >= -8 && db_dg <= 7) {
        out[p++] = BFG_OP_DELTA2 | (uint8_t)((dg + 32) & 0x3F);
        out[p++] = (uint8_t)(((dr_dg + 8) << 4) | ((db_dg + 8) & 0x0F));
      }
      /* RGB literal */
      else if (px.a == prev.a) {
        out[p++] = BFG_OP_RGB;
        out[p++] = px.r;
        out[p++] = px.g;
        out[p++] = px.b;
      }
      /* RGBA literal */
      else {
        out[p++] = BFG_OP_RGBA;
        out[p++] = px.r;
        out[p++] = px.g;
        out[p++] = px.b;
        out[p++] = px.a;
      }

      cache[bfg_hash(px)] = px;
      prev_row[x] = px;
      left = px;
      prev = px;
    }
  }

  /* flush final run */
  if (run > 0) {
    if (run <= 32) {
      out[p++] = BFG_OP_RUN | (run - 1);
    } else {
      out[p++] = BFG_OP_RUN | 31;
      out[p++] = BFG_OP_RUN2;
      out[p++] = (uint8_t)(run - 33);
    }
  }

  BFG_FREE(prev_row);
  *out_len = p;
  return out;
}

/* ---- decoder ---- */

int bfg_decode(const bfg_header_t *header, const uint8_t *data,
               uint32_t data_len, bfg_raw_t raw) {
  if (!header || !data || !raw) return 1;

  uint32_t w = header->width;
  uint32_t h = header->height;
  uint8_t ch = header->channels;
  if (!w || !h || ch < 3 || ch > 4) return 1;

  uint64_t n_px = (uint64_t)w * h;
  if (n_px > BFG_MAX_PIXELS) return 1;

  raw->width = w;
  raw->height = h;
  raw->n_channels = ch;
  raw->pixels = (uint8_t *)BFG_MALLOC((size_t)(n_px * ch));
  if (!raw->pixels) return 1;

  bfg_pixel_t cache[BFG_CACHE_SIZE];
  memset(cache, 0, sizeof(cache));

  bfg_pixel_t *prev_row = (bfg_pixel_t *)BFG_MALLOC(w * sizeof(bfg_pixel_t));
  if (!prev_row) { BFG_FREE(raw->pixels); raw->pixels = NULL; return 1; }

  for (uint32_t x = 0; x < w; x++) {
    prev_row[x].r = 0; prev_row[x].g = 0;
    prev_row[x].b = 0; prev_row[x].a = 255;
  }

  bfg_pixel_t prev = {0, 0, 0, 255};
  uint32_t dp = 0; /* data pointer */
  uint32_t px_x = 0, px_y = 0; /* current pixel coords */
  bfg_pixel_t left = {0, 0, 0, 255};

  while (px_y < h && dp < data_len) {
    uint8_t b0 = data[dp];

    /* decode op */
    bfg_pixel_t px;

    if ((b0 & BFG_MASK1) == BFG_OP_DELTA1) {
      /* 0 | dg+4(3) | (dr-dg)+2(2) | (db-dg)+2(2) */
      bfg_pixel_t above = prev_row[px_x];
      bfg_pixel_t pred;
      if (px_y == 0) pred = left;
      else if (px_x == 0) pred = above;
      else pred = bfg_predict(left, above);

      int dg = ((b0 >> 4) & 0x07) - 4;
      int dr_dg = ((b0 >> 2) & 0x03) - 2;
      int db_dg = (b0 & 0x03) - 2;
      px.r = (uint8_t)((int)pred.r + dg + dr_dg);
      px.g = (uint8_t)((int)pred.g + dg);
      px.b = (uint8_t)((int)pred.b + dg + db_dg);
      px.a = prev.a;
      dp += 1;
    }
    else if ((b0 & BFG_MASK2) == BFG_OP_DELTA2) {
      if (dp + 1 >= data_len) break;
      bfg_pixel_t above = prev_row[px_x];
      bfg_pixel_t pred;
      if (px_y == 0) pred = left;
      else if (px_x == 0) pred = above;
      else pred = bfg_predict(left, above);

      uint8_t b1 = data[dp + 1];
      int dg = (b0 & 0x3F) - 32;
      int dr_dg = ((b1 >> 4) & 0x0F) - 8;
      int db_dg = (b1 & 0x0F) - 8;
      px.r = (uint8_t)((int)pred.r + dg + dr_dg);
      px.g = (uint8_t)((int)pred.g + dg);
      px.b = (uint8_t)((int)pred.b + dg + db_dg);
      px.a = prev.a;
      dp += 2;
    }
    else if ((b0 & BFG_MASK3) == BFG_OP_RUN) {
      uint32_t run_len = (b0 & 0x1F) + 1;
      /* check for extended RUN2 following a full RUN of 32 */
      if (run_len == 32 && dp + 1 < data_len && data[dp + 1] == BFG_OP_RUN2) {
        if (dp + 2 >= data_len) break;
        run_len += (uint32_t)data[dp + 2] + 1;
        dp += 3;
      } else {
        dp += 1;
      }
      for (uint32_t i = 0; i < run_len && px_y < h; i++) {
        bfg_write_pixel(&raw->pixels[(px_y * w + px_x) * ch], prev, ch);
        prev_row[px_x] = prev;
        left = prev;
        px_x++;
        if (px_x == w) {
          px_x = 0;
          px_y++;
          left.r = 0; left.g = 0; left.b = 0; left.a = 255;
        }
      }
      continue; /* skip the single-pixel write below */
    }
    else if ((b0 & BFG_MASK4) == BFG_OP_CACHE) {
      uint32_t ci = b0 & 0x0F;
      px = cache[ci];
      dp += 1;
    }
    else if (b0 == BFG_OP_RGB) {
      if (dp + 3 >= data_len) break;
      px.r = data[dp + 1];
      px.g = data[dp + 2];
      px.b = data[dp + 3];
      px.a = prev.a;
      dp += 4;
    }
    else if (b0 == BFG_OP_RGBA) {
      if (dp + 4 >= data_len) break;
      px.r = data[dp + 1];
      px.g = data[dp + 2];
      px.b = data[dp + 3];
      px.a = data[dp + 4];
      dp += 5;
    }
    else {
      /* unknown op — data corruption */
      BFG_FREE(prev_row);
      BFG_FREE(raw->pixels);
      raw->pixels = NULL;
      return 1;
    }

    /* write pixel and advance */
    bfg_write_pixel(&raw->pixels[(px_y * w + px_x) * ch], px, ch);
    cache[bfg_hash(px)] = px;
    prev_row[px_x] = px;
    left = px;
    prev = px;

    px_x++;
    if (px_x == w) {
      px_x = 0;
      px_y++;
      left.r = 0; left.g = 0; left.b = 0; left.a = 255;
    }
  }

  BFG_FREE(prev_row);
  return 0;
}

/* ---- file I/O ---- */

static void write_u32_le(uint8_t *buf, uint32_t v) {
  buf[0] = (uint8_t)(v);
  buf[1] = (uint8_t)(v >> 8);
  buf[2] = (uint8_t)(v >> 16);
  buf[3] = (uint8_t)(v >> 24);
}

static uint32_t read_u32_le(const uint8_t *buf) {
  return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
         ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

int bfg_write(const char *fpath, const bfg_header_t *header,
              const uint8_t *data, uint32_t data_len) {
  if (!fpath || !header || !data) return 1;
  FILE *fp = fopen(fpath, "wb");
  if (!fp) return 1;

  uint8_t hdr[BFG_HEADER_SIZE];
  write_u32_le(&hdr[0], header->magic);
  write_u32_le(&hdr[4], header->width);
  write_u32_le(&hdr[8], header->height);
  hdr[12] = header->channels;
  hdr[13] = 0; hdr[14] = 0; hdr[15] = 0;

  if (fwrite(hdr, 1, BFG_HEADER_SIZE, fp) != BFG_HEADER_SIZE) {
    fclose(fp); return 1;
  }
  if (fwrite(data, 1, data_len, fp) != data_len) {
    fclose(fp); return 1;
  }
  fclose(fp);
  return 0;
}

uint8_t *bfg_read(const char *fpath, bfg_header_t *header, uint32_t *out_len) {
  if (!fpath || !header || !out_len) return NULL;
  FILE *fp = fopen(fpath, "rb");
  if (!fp) return NULL;

  uint8_t hdr[BFG_HEADER_SIZE];
  if (fread(hdr, 1, BFG_HEADER_SIZE, fp) != BFG_HEADER_SIZE) {
    fclose(fp); return NULL;
  }

  header->magic = read_u32_le(&hdr[0]);
  header->width = read_u32_le(&hdr[4]);
  header->height = read_u32_le(&hdr[8]);
  header->channels = hdr[12];
  memset(header->reserved, 0, 3);

  if (header->magic != BFG_MAGIC) {
    fprintf(stderr, "Not a valid BFG2 file\n");
    fclose(fp); return NULL;
  }

  /* determine data size */
  long cur = ftell(fp);
  fseek(fp, 0, SEEK_END);
  long end = ftell(fp);
  fseek(fp, cur, SEEK_SET);
  uint32_t data_len = (uint32_t)(end - cur);

  uint8_t *data = (uint8_t *)BFG_MALLOC(data_len);
  if (!data) { fclose(fp); return NULL; }
  if (fread(data, 1, data_len, fp) != data_len) {
    BFG_FREE(data); fclose(fp); return NULL;
  }

  fclose(fp);
  *out_len = data_len;
  return data;
}

/* ---- free ---- */

void bfg_free_raw(bfg_raw_t raw) {
  if (raw && raw->pixels) {
    BFG_FREE(raw->pixels);
    raw->pixels = NULL;
  }
}

void bfg_free_img(bfg_img_t img) {
  if (img) BFG_FREE(img);
}
