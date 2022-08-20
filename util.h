#include <stdio.h>
#include <time.h>

#define FLAT_INDEX(x, y, w) ((y) * (w) + (x))
#define FCLOSE(fp) ((fp) ? fclose((fp)) : 0, (fp) = NULL)
#define BIT_MASK(width, offset) ((~(~0ULL << (width)) << (offset)))
#define TWO_POWER(pow) (1 << (pow))
#define IN_RANGE(val, min, max) ((val - min) * (max - val) >= 0)
#define CEIL_DIV(num, den) (((num)-1) / (den) + 1)
#define MILLIS_SINCE(time) (((double)clock() - (time)) * 1000 / CLOCKS_PER_SEC)

#define WRAP_DIFF(diff, min_diff, max_diff, color_range)                       \
  (IN_RANGE((diff), -(color_range) + 1, -(color_range) + (max_diff))           \
       ? ((diff) + (color_range))                                              \
       : (IN_RANGE((diff), (color_range) + (min_diff), (color_range)-1)        \
              ? ((diff) - (color_range))                                       \
              : (diff)))

// write width bits of value to byte_ptr, shifted offset bits to the left
// so if *p = 0b0100001, after WRITE_BITS(p, 0b101, 3, 2), *p = 0b0110101
#define WRITE_BITS(byte_ptr, value, width, offset)                             \
  ((*(uint8_t *)(byte_ptr)) =                                                  \
       (((*(uint8_t *)(byte_ptr)) & ~BIT_MASK((width), (offset))) |            \
        (BIT_MASK((width), (offset)) & ((value) << (offset)))))

// read width bits of value from byte_ptr at offset
// so if *p = 0b0110101, READ_BITS(p, 4, 2) = 0b1101
#define READ_BITS(byte_ptr, width, offset)                                     \
  (((*(uint8_t *)(byte_ptr)) >> (offset)) & BIT_MASK((width), 0))
