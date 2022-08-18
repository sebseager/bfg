#include "bfg.h"
#include <png.h>
#include <stdint.h>

#define CONV_BIT_DEPTH (8)

/* -------------- */
/* PNG conversion */
/* -------------- */

typedef struct png_data {
  FILE *fp;
  char file_mode; // either 'r' for read or 'w' for write
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
 * Returns 0 on success, nonzero on failure. */
int libpng_decode(png_data_t png, bfg_raw_t raw);

/* Allocates and populates libpng data struct with data from bfg data struct and
 * writes it to the file specified by fpath.
 * Returns 0 on success, nonzero on failure. */
int libpng_write(char *fpath, bfg_raw_t raw);


