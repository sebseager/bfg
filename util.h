#ifndef BFG_UTIL_H
#define BFG_UTIL_H

#include <stdio.h>
#include <time.h>

#define FLAT_INDEX(x, y, w) ((y) * (w) + (x))
#define FCLOSE(fp) ((fp) ? fclose((fp)) : 0, (fp) = NULL)
#define MILLIS_SINCE(time) (((double)clock() - (time)) * 1000 / CLOCKS_PER_SEC)

#endif /* BFG_UTIL_H */
