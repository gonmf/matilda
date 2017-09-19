/*
Implementation of CRC32 hashing for generic data.
*/

#ifndef MATILDA_CRC32_H
#define MATILDA_CRC32_H

#include "config.h"

#include "types.h"
#include "board.h"

/*
Generate a CRC32 hash of the data specified.
RETURNS CRC32 hash
*/
u32 crc32(
    const void * buf,
    u32 size
);

#endif
