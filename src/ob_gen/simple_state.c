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
#include "buffer.h"


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

static u64 get_total_count(
    simple_state_transition * s
){
    u32 ret = 0;
    for(move i = 0; i < BOARD_SIZ * BOARD_SIZ; ++i)
        ret += s->count[i];
    return ret;
}

/*
Exports internal OB table to simple OB format in file.
*/
void simple_state_collection_export(
    u32 min_samples
){
    char * str = malloc(MAX_PAGE_SIZ);

    snprintf(str, 1024, "%soutput.ob", get_data_folder());

    FILE * fp = fopen(str, "w");
    if(fp == NULL)
    {
        fprintf(stderr,
            "error: failed to open opening book file for writing\n");
        exit(EXIT_FAILURE);
    }

    u32 skipped = 0;
    u32 exported = 0;
    u32 idx;

    for(u32 ti = 0; ti < SS_TABLE_SIZ; ++ti)
    {
        simple_state_transition * h = sst_table[ti];
        while(h != NULL)
        {
            u32 total_count = get_total_count(h);
            if(total_count < min_samples)
            {
                ++skipped;
                h = h->next;
                continue;
            }

            u32 best_count = 0;
            move best = NONE;
            for(move i = 0; i < BOARD_SIZ * BOARD_SIZ; ++i)
                if(h->count[i] > best_count)
                {
                    best_count = h->count[i];
                    best = i;
                }

            if(best == NONE)
            {
                fprintf(stderr, "error: unexpected absence of samples\n");
                exit(EXIT_FAILURE);
            }

            if(best_count <= total_count / 2)
            {
                ++skipped;
                h = h->next;
                continue;
            }

            u8 p[BOARD_SIZ * BOARD_SIZ];
            unpack_matrix(p, h->p);

            move m1 = 0;
            move m2 = 0;
            bool is_black = true;

            str[0] = 0;
            idx = 0;
            idx += snprintf(str + idx, MAX_PAGE_SIZ - idx, "%u ", BOARD_SIZ);

            while(1)
            {
                bool found = false;
                if(is_black)
                {
                    for(; m1 < BOARD_SIZ * BOARD_SIZ; ++m1)
                        if(p[m1] == BLACK_STONE)
                        {
                            idx += snprintf(str + idx, MAX_PAGE_SIZ - idx,
                                "%s ", coord_to_alpha_num(m1));
                            found = true;
                            ++m1;
                            break;
                        }
                }else
                    for(; m2 < BOARD_SIZ * BOARD_SIZ; ++m2)
                        if(p[m2] == WHITE_STONE)
                        {
                            idx += snprintf(str + idx, MAX_PAGE_SIZ - idx,
                                "%s ", coord_to_alpha_num(m1));
                            found = true;
                            ++m2;
                            break;
                        }
                if(!found)
                    break;
            }

            snprintf(str + idx, MAX_PAGE_SIZ - idx, "| %s # %u/%u\n",
                coord_to_alpha_num(best), best_count, total_count);


            size_t w = fwrite(str, strlen(str), 1, fp);
            if(w != 1)
            {
                fprintf(stderr, "error: write failed\n");
                exit(EXIT_FAILURE);
            }

            ++exported;
            h = h->next;
        }
    }

    snprintf(str, 1024, "# exported %u unique rules; %u were disqualified for \
not enough samples or majority representative\n",exported, skipped);
    size_t w = fwrite(str, strlen(str), 1, fp);
    if(w != 1)
    {
        fprintf(stderr, "error: write failed\n");
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    free(str);

    printf("exported %u unique rules; %u were disqualified for not enough \
samples or majority representative\n", exported, skipped);
}
