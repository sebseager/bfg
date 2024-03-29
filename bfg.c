#include "bfg.h"
#include "util.h"
#include <stdlib.h>

void bfg_free(bfg_raw_t raw, bfg_img_t img) {
  if (raw) {
    BFG_FREE(raw->pixels);
  }
  if (img) {
    BFG_FREE(img);
  }
}

bfg_img_t bfg_encode(bfg_raw_t raw, bfg_info_t info) {
  if (!raw || !info || !raw->width || !raw->height || !raw->n_channels) {
    return NULL;
  }

  info->magic_tag = BFG_MAGIC_TAG;
  info->version = BFG_VERSION;
  info->width = raw->width;
  info->height = raw->height;
  info->n_bytes = 0;
  info->n_channels = raw->n_channels;
  info->color_mode = 0; // this doesn't do anything at the moment

  // ensure we never need more than BFG_MAX_BYTES bytes
  // add extra channel to account for worst case encoding
  if (info->width >= BFG_MAX_BYTES / info->height / (info->n_channels + 1)) {
    return NULL;
  }

  const uint32_t n_px = info->width * info->height;
  const uint32_t image_bytes = n_px * (info->n_channels + 1);
  bfg_img_t img = (bfg_img_t)BFG_MALLOC(image_bytes);
  if (!img) {
    return NULL;
  }

  const unsigned int max_block_entries =
      TWO_POWER(BFG_BIT_DEPTH - BFG_TAG_BITS) - 1;
  const int color_range = TWO_POWER(BFG_BIT_DEPTH);

  // e.g. for 4 diff bits, allowed range is [-16,15]
  //      0000 = 0, 0001 = 1, 1000 = -1, 1001 = -2
  const int max_diff = TWO_POWER(BFG_DIFF_BITS - 1) - 1;
  const int min_diff = -max_diff - 1;

  uint32_t block_header_idx = 0;
  uint32_t block_len = 0;

  for (uint8_t c = 0; c < info->n_channels; c++) {
    bfg_block_type_t active_block = BFG_BLOCK_FULL;
    bfg_block_type_t next_block = BFG_BLOCK_FULL;

    uint32_t read_i = 0;
    uint8_t prev = 0;
    uint8_t curr = raw->pixels[c];
    uint8_t next[2];

    // account for images with fewer than 3 pixels
    next[0] = raw->pixels[n_px > 1 ? 1 * info->n_channels + c : c];
    next[1] = raw->pixels[n_px > 2 ? (unsigned)(2 * info->n_channels + c)
                                   : (unsigned)(n_px - 1 + c)];

    int did_encode_px = 0;
    int do_change_block = 0;

    while (read_i < n_px) {
      int diff = curr - prev;
      int next_diff = next[0] - curr;
      int next_next_diff = next[1] - next[0];
      diff = WRAP_DIFF(diff, min_diff, max_diff, color_range);
      next_diff = WRAP_DIFF(next_diff, min_diff, max_diff, color_range);
      next_next_diff =
          WRAP_DIFF(next_next_diff, min_diff, max_diff, color_range);

      const int can_continue_run = diff == 0;
      const int can_start_run =
          can_continue_run && next_diff == 0 && next_next_diff == 0;
      const int can_continue_diff = IN_RANGE(diff, min_diff, max_diff);
      const int can_start_diff = can_continue_diff &&
                                 IN_RANGE(next_diff, min_diff, max_diff) &&
                                 IN_RANGE(next_next_diff, min_diff, max_diff);

      // either extend current block or switch to new block
      switch (active_block) {
      case BFG_BLOCK_FULL: {
        if (block_len > max_block_entries) {
          do_change_block = 1;
        }
        if (can_start_run) {
          do_change_block = 1;
          next_block = BFG_BLOCK_RUN;
        } else if (can_start_diff) {
          do_change_block = 1;
          next_block = BFG_BLOCK_DIFF;
        }
        if (!do_change_block) {
          did_encode_px = 1;
          block_len++;
          WRITE_BITS(&img[block_header_idx + block_len], curr, BFG_BIT_DEPTH,
                     0);
        }
        break;
      }

      case BFG_BLOCK_RUN: {
        if (block_len > max_block_entries) {
          do_change_block = 1;
          next_block = BFG_BLOCK_FULL;
        }
        if (!do_change_block && can_continue_run) {
          did_encode_px = 1;
          block_len++;
        } else if (can_start_diff) {
          do_change_block = 1;
          next_block = BFG_BLOCK_DIFF;
        } else {
          do_change_block = 1;
          next_block = BFG_BLOCK_FULL;
        }
        break;
      }

      case BFG_BLOCK_DIFF: {
        if (block_len > max_block_entries) {
          do_change_block = 1;
          next_block = BFG_BLOCK_FULL;
        }
        uint32_t offset_bits =
            (BFG_BIT_DEPTH - block_len * BFG_DIFF_BITS) % BFG_BIT_DEPTH;
        if (offset_bits == 0) {
          // we're at a byte boundary, so good place to switch
          if (can_start_run) {
            do_change_block = 1;
            next_block = BFG_BLOCK_RUN;
          }
        }
        if (!do_change_block && can_continue_diff) {
          did_encode_px = 1;
          block_len++;
          offset_bits = (offset_bits - BFG_DIFF_BITS) % BFG_BIT_DEPTH;
          uint32_t bytes_ahead =
              CEIL_DIV(block_len * BFG_DIFF_BITS, BFG_BIT_DEPTH);
          uint8_t *dest = &img[block_header_idx + bytes_ahead];
          if (diff < 0) {
            WRITE_BITS(dest, 1, 1, offset_bits + BFG_DIFF_BITS - 1);
            WRITE_BITS(dest, abs(diff) - 1, BFG_DIFF_BITS - 1, offset_bits);
          } else {
            WRITE_BITS(dest, 0, 1, offset_bits + BFG_DIFF_BITS - 1);
            WRITE_BITS(dest, diff, BFG_DIFF_BITS - 1, offset_bits);
          }
        } else {
          do_change_block = 1;
          next_block = BFG_BLOCK_FULL;
        }
        break;
      }
      }

      // end block (either switch blocks or reached end of channel)
      if (do_change_block || read_i == n_px - 1) {
        // if block was empty we don't need to write anything
        if (block_len > 0) {
          // write old block's header
          WRITE_BITS(&img[block_header_idx], active_block, BFG_TAG_BITS,
                     BFG_BIT_DEPTH - BFG_TAG_BITS);
          WRITE_BITS(&img[block_header_idx], block_len - 1,
                     BFG_BIT_DEPTH - BFG_TAG_BITS, 0);

          // some blocks take up less than block_len bytes
          uint32_t block_bytes;
          switch (active_block) {
          case BFG_BLOCK_RUN:
            block_bytes = 0;
            break;
          case BFG_BLOCK_DIFF:
            block_bytes = CEIL_DIV(block_len * BFG_DIFF_BITS, BFG_BIT_DEPTH);
            break;
          default:
            block_bytes = block_len;
            break;
          }

          info->n_bytes += block_bytes + 1;
          block_header_idx += block_bytes + 1;
          block_len = 0;
        }

        active_block = next_block;
        do_change_block = 0;
      }

      // advance to next pixel
      if (did_encode_px) {
        read_i++;
        did_encode_px = 0;

        prev = curr;
        curr = next[0];
        next[0] = next[1];
        if (read_i < n_px - 2) {
          next[1] = raw->pixels[(read_i + 2) * info->n_channels + c];
        }
      }
    }
  }

  return img;
}

int bfg_decode(bfg_info_t info, bfg_img_t img, bfg_raw_t raw) {
  if (!info || !img || !raw) {
    return 1;
  }

  raw->width = info->width;
  raw->height = info->height;
  raw->n_channels = info->n_channels;

  if (!info->width | !info->height | !info->n_channels) {
    return 1;
  }

  // ensure we never need more than BFG_MAX_BYTES bytes
  if (raw->width >= BFG_MAX_BYTES / raw->height / raw->n_channels) {
    return 1;
  }

  const uint32_t total_px = raw->width * raw->height;
  const uint32_t total_bytes = total_px * raw->n_channels;
  raw->pixels = (bfg_img_t)BFG_MALLOC(total_bytes);
  if (!raw->pixels) {
    return 1;
  }

  uint32_t block_header_idx = 0;
  uint8_t channel = 0;
  uint32_t px_i = 0;
  uint8_t prev = 0;

  while (block_header_idx < info->n_bytes) {
    const bfg_block_type_t block_type = (bfg_block_type_t)READ_BITS(
        &img[block_header_idx], BFG_TAG_BITS, BFG_BIT_DEPTH - BFG_TAG_BITS);
    uint32_t block_len =
        READ_BITS(&img[block_header_idx], BFG_BIT_DEPTH - BFG_TAG_BITS, 0) + 1;
    uint32_t block_bytes = 0; // set in each case below

    // process block
    switch (block_type) {
    case BFG_BLOCK_FULL: {
      const uint32_t block_start = block_header_idx + 1;
      block_bytes = block_len;
      const uint32_t block_end = block_start + block_bytes;
      for (uint32_t i = block_start; i < block_end; i++) {
        raw->pixels[(px_i++) * raw->n_channels + channel] = img[i];
        prev = img[i];

        // DEBUG
        // printf("FL %d:\t%d\n", px_i - 1, prev);
      }
      break;
    }

    case BFG_BLOCK_RUN: {
      block_bytes = 0;
      for (uint32_t i = 0; i < block_len; i++) {
        raw->pixels[(px_i++) * raw->n_channels + channel] = prev;

        // DEBUG
        // printf("RN %d:\t%d\n", px_i - 1, prev);
      }
      break;
    }

    case BFG_BLOCK_DIFF: {
      const uint32_t block_start = block_header_idx + 1;
      block_bytes = CEIL_DIV(block_len * BFG_DIFF_BITS, BFG_BIT_DEPTH);
      for (uint32_t i = block_start; i < block_start + block_bytes; i++) {
        int offset_bits = BFG_BIT_DEPTH - BFG_DIFF_BITS;
        while (offset_bits >= 0) {
          uint8_t *dest = &img[i];
          int16_t diff = READ_BITS(dest, BFG_DIFF_BITS - 1, offset_bits);
          if (READ_BITS(dest, 1, offset_bits + BFG_DIFF_BITS - 1) == 1) {
            diff = -diff - 1;
          }
          prev += diff;
          raw->pixels[(px_i++) * raw->n_channels + channel] = prev;
          offset_bits -= BFG_DIFF_BITS;

          // DEBUG
          // printf("DF %d:\t%d\n", px_i - 1, prev);

          // we might need to end early if we've exhausted block_len
          block_len--;
          if (block_len == 0) {
            break;
          }
        }
      }
      break;
    }
    }

    block_header_idx += block_bytes + 1;

    if (px_i == total_px) {
      channel++;
      px_i = 0;
      prev = 0;
    }
  }

  return 0;
}

int bfg_write(char *fpath, bfg_info_t info, bfg_img_t img) {
  FILE *fp = fopen(fpath, "wb");
  if (!fpath || !info || !img || !fp) {
    return 1;
  }

  fwrite(info, sizeof(struct bfg_info), 1, fp);
  fwrite(img, info->n_bytes, 1, fp);
  FCLOSE(fp);
  return 0;
}

bfg_img_t bfg_read(char *fpath, bfg_info_t info) {
  FILE *fp = fopen(fpath, "rb");
  if (!fpath || !info || !fp) {
    return NULL;
  }

  fread(info, sizeof(struct bfg_info), 1, fp);

  if (info->magic_tag != BFG_MAGIC_TAG) {
    printf("Not a valid bfg file\n");
    return NULL;
  }
  if (info->version != BFG_VERSION) {
    printf("Unsupported bfg version\n");
    return NULL;
  }

  bfg_img_t img = (bfg_img_t)BFG_MALLOC(info->n_bytes);
  fread(img, info->n_bytes, 1, fp);
  FCLOSE(fp);
  return img;
}
