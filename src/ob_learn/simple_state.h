#ifndef __SSTATE_COLLECTIONS_
#define __SSTATE_COLLECTIONS_

#include "matilda.h"

#include "types.h"
#include "move.h"
#include "board.h"

typedef struct __simple_state_transition_ {
    u8 p[PACKED_BOARD_SIZ];
    move play;
    u32 popularity;
    u32 hash;
    struct __simple_state_transition_ * next;
} simple_state_transition;

simple_state_transition * simple_state_collection_find(
    u32 hash,
    const u8 p[PACKED_BOARD_SIZ]
);

void simple_state_collection_add(
    simple_state_transition * s
);

simple_state_transition * simple_state_collection_export();

void simple_state_table_init();

#endif
