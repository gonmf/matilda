#ifndef __CSTATE_COLLECTIONS_
#define __CSTATE_COLLECTIONS_

#include "matilda.h"
#include "types.h"

typedef struct __complete_state_transition_ {
    u8 p[BOARD_SIZ * BOARD_SIZ];
    u32 count[BOARD_SIZ * BOARD_SIZ];
    struct __complete_state_transition_ * next;
} complete_state_transition;


/*
Initialize table collection table.
*/
void cs_table_init();

/*
Find state transition in collection.
*/
complete_state_transition * complete_state_collection_find(
    complete_state_transition * s
);

/*
Add state transition to collection.
*/
void complete_state_collection_add(
    complete_state_transition * s
);

/*
Exporting as a training set involves choosing one of the candidate plays as
representative play for that training case. This is done by number of
occurrences.
*/
void complete_state_collection_export_as_data_set(
    u32 expected_elems
);

#endif
