#include "matilda.h"

#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include "simple_state.h"
#include "opening_book.h"
#include "engine.h"

#define SS_TABLE_SIZ 12289 /* prime number */

static simple_state_transition ** sst_table;

void simple_state_table_init()
{
    if(sst_table == NULL)
    {
        sst_table = (simple_state_transition **)calloc(SS_TABLE_SIZ,
            sizeof(simple_state_transition *));
        if(sst_table == NULL)
        {
            fprintf(stderr, "error: sst table init: system out of memory\n");
            exit(EXIT_FAILURE);
        }
    }
}

simple_state_transition * simple_state_collection_find(
    u32 hash,
    const u8 p[PACKED_BOARD_SIZ]
){
    simple_state_transition * h = sst_table[hash % SS_TABLE_SIZ];
    while(h != NULL)
    {
        if(hash == h->hash && memcmp(&h->p, p, PACKED_BOARD_SIZ) == 0)
            return h;
        h = h->next;
    }
    return NULL;
}

void simple_state_collection_add(
    simple_state_transition * s
){
    s->next = sst_table[s->hash % SS_TABLE_SIZ];
    sst_table[s->hash % SS_TABLE_SIZ] = s;
}

static simple_state_transition * get_tail(
    simple_state_transition * p
){
    if(p->next == NULL)
        return p;
    return get_tail(p->next);
}

simple_state_transition * simple_state_collection_export()
{
    for(u32 i = 0; i < SS_TABLE_SIZ - 1; ++i)
    {
        if(sst_table[i] == NULL)
            continue;
        simple_state_transition * tail = get_tail(sst_table[i]);
        for(u32 j = i + 1; j < SS_TABLE_SIZ; ++j)
            if(sst_table[j] != NULL)
            {
                tail->next = sst_table[j];
                break;
            }
    }
    get_tail(sst_table[SS_TABLE_SIZ - 1])->next = NULL;
    return sst_table[0];
}
