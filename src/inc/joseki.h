/*
Strategy that makes use of an opening book.
*/

#ifndef MATILDA_JOSEKI_H
#define MATILDA_JOSEKI_H

#include "matilda.h"

#include "types.h"
#include "move.h"

#define MAX_JOSEKI_SUGGESTIONS 32
#define JOSEKI_REGION_SIZ 10
#define MIN_BOARD_SIZ_FOR_JOSEKI_USE 12

typedef struct __joseki_ {
    u32 hash;
    u8 p[PACKED_BOARD_SIZ];
    u8 plays_count;
    move plays[MAX_JOSEKI_SUGGESTIONS];
    struct __joseki_ * next;
} joseki;


#endif
