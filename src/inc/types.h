#ifndef MATILDA_TYPES_H
#define MATILDA_TYPES_H

#include "config.h"

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
For reading files MAX_FILE_SIZ is recommended. Even SGF files can become large
if with a lot of commentary.
*/
#define MAX_PATH_SIZ (1024 - 16)              /* 1 KiB */
#define MAX_PAGE_SIZ (4 * 1024 - 32)        /* 4 KiB */
#define MAX_FILE_SIZ (4 * 1024 * 1024 - 64) /* 4 MiB */

#endif
