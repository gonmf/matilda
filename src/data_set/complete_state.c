#include "matilda.h"

#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include "alloc.h"
#include "complete_state.h"
#include "crc32.h"
#include "data_set.h"
#include "engine.h"
#include "flog.h"
#include "hash_table.h"

#define CS_TABLE_SIZ 216091 /* prime number */

static complete_state_transition ** cst_table;

/*
Initialize table collection table.
*/
void cs_table_init()
{
    if(cst_table == NULL)
    {
        cst_table = (complete_state_transition **)calloc(CS_TABLE_SIZ,
            sizeof(complete_state_transition *));
        if(cst_table == NULL)
            flog_crit("cst", "system out of memory");
    }
}

/*
Find state transition in collection.
*/
complete_state_transition * complete_state_collection_find(
    complete_state_transition * s)
{
    u32 hash = crc32(s->p, BOARD_SIZ * BOARD_SIZ);
    complete_state_transition * h = cst_table[hash % CS_TABLE_SIZ];
    while(h != NULL)
    {
        if(memcmp(&h->p, &s->p, BOARD_SIZ * BOARD_SIZ) == 0)
            return h;
        h = h->next;
    }
    return NULL;
}

/*
Add state transition to collection.
*/
void complete_state_collection_add(
    complete_state_transition * s
){
    u32 hash = crc32(s->p, BOARD_SIZ * BOARD_SIZ);
    s->next = cst_table[hash % CS_TABLE_SIZ];
    cst_table[hash % CS_TABLE_SIZ] = s;
}

/*
Exporting as a training set involves choosing one of the candidate plays as
representative play for that training case. This is done by number of
occurrences.
*/
void complete_state_collection_export_as_data_set(
    u32 expected_elems
){
    training_example te;
    u32 written = 0;

    char * filename = alloc();
    snprintf(filename, MAX_PAGE_SIZ, "%s%dx%d.ds", data_folder(), BOARD_SIZ,
        BOARD_SIZ);
    FILE * fp = fopen(filename, "wb");
    if(fp == NULL)
        flog_crit("cst", "couldn't open file for writing");
    release(filename);

    size_t w = fwrite(&expected_elems, sizeof(u32), 1, fp);
    if(w != 1)
        flog_crit("cst", "write failed");

    for(u32 i = 0; i < CS_TABLE_SIZ; ++i)
    {
        complete_state_transition * h = cst_table[i];
        while(h != NULL)
        {
            memcpy(&te.p, &h->p, BOARD_SIZ * BOARD_SIZ);
            u32 best_count = 0;
            for(move k = 0; k < BOARD_SIZ * BOARD_SIZ; ++k)
                if(h->count[k] > best_count)
                {
                    best_count = h->count[k];
                    te.m = k;
                }

            size_t w = fwrite(&te, sizeof(training_example), 1, fp);
            if(w != 1)
                flog_crit("cst", "write failed\n");
            ++written;
            h = h->next;
        }
    }

    fclose(fp);

    if(expected_elems != written)
        flog_crit("cst",
            "mismatch between written and expected elements to write");
}
