#ifndef BFG_CONVERT_H
#define BFG_CONVERT_H

#include "bfg.h"
#include <png.h>

/* -------------- */
/* PNG conversion */
/* -------------- */

typedef struct png_data {
  FILE *fp;
  char file_mode; /* 'r' for read, 'w' for write */
  png_structp png_ptr;
  png_infop info_ptr;
  png_bytep *row_ptrs;
} * png_data_t;

/* Frees everything allocated within the libpng data struct. */
void libpng_free(png_data_t png);

/* Reads png file at fpath into png data struct using the libpng API. Caller is
 * responsible for freeing the internals of the png struct with libpng_free.
 * Returns 0 on success, nonzero on failure. */
int libpng_read(char *fpath, png_data_t png);

/* Populates raw image data struct with data from libpng data struct.
 * Grayscale images are expanded to RGB(A) so BFG always gets 3 or 4 channels.
 * Returns 0 on success, nonzero on failure. */
int libpng_decode(png_data_t png, bfg_raw_t raw);

/* Writes raw image data to a PNG file at fpath.
 * Returns 0 on success, nonzero on failure. */
int libpng_write(char *fpath, bfg_raw_t raw);

#endif /* BFG_CONVERT_H */
