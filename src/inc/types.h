#ifndef MATILDA_TYPES_H
#define MATILDA_TYPES_H

#include "matilda.h"

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h> /* for printf format portability */

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef  uint8_t  u8;
typedef  int64_t d64;
typedef  int32_t d32;
typedef  int16_t d16;
typedef   int8_t  d8;

/*
For reading small files like SGF, MAX_PAGE_SIZ is enough.
For reading .ob, .zt and weights files MAX_FILE_SIZ is recommended.
*/
#define MAX_PATH_SIZ 1024              /* 1 KiB */
#define MAX_PAGE_SIZ (4 * 1024)        /* 4 KiB */
#define MAX_FILE_SIZ (4 * 1024 * 1024) /* 4 MiB */

#endif
