/*
Data set collection manipulation functions.

A data set file is defined as 4 bytes (unsigned int) indicating the
number of training cases, followed by the elements of struct
training_example type.

The examples are stored unique and invariant of flips and rotations, when
loaded via the data_set_load function they are fliped and rotated to increase
the data set size.
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "alloc.h"
#include "data_set.h"
#include "engine.h"
#include "flog.h"
#include "randg.h"
#include "types.h"

static u32 data_set_size;
static training_example ** data_set = NULL;

/*
Shuffle all first num entries.
Fisher–Yates shuffle.
*/
void data_set_shuffle(
    u32 num
) {
    assert(num > 1 && num <= data_set_size);

    u32 i;
    for (i = num - 1; i > 0; --i) {
        u32 j = rand_u32(i + 1);
        training_example * tmp = data_set[i];
        data_set[i] = data_set[j];
        data_set[j] = tmp;
    }
}

/*
Shuffle all data set.
Fisher–Yates shuffle.
*/
void data_set_shuffle_all() {
    data_set_shuffle(data_set_size);
}

/*
Read a data set and shuffle it.
RETURNS table set size (number of cases)
*/
u32 data_set_load() {
    return data_set_load2(UINT32_MAX);
}

/*
Read a data set, with a maximum size, and shuffles it.
RETURNS table set size (number of cases)
*/
u32 data_set_load2(
    u32 max
) {
    assert(data_set == NULL);

    char * filename = alloc();
    snprintf(filename, MAX_PAGE_SIZ, "%s%dx%d.ds", data_folder(), BOARD_SIZ, BOARD_SIZ);
    FILE * fp = fopen(filename, "rb");
    release(filename);

    if (fp == NULL) {
        flog_crit("dset", "could not open file for reading\n");
    }

    u32 ds_elems;
    size_t r = fread(&ds_elems, sizeof(u32), 1, fp);

    if (r != 1) {
        flog_crit("dset", "communication failure\n");
    }

    assert(ds_elems > 0);

    ds_elems = MIN(ds_elems, max);

    data_set = malloc(sizeof(training_example *) * ds_elems * 8);
    if (data_set == NULL) {
        flog_crit("dset", "system out of memory\n");
    }

    u32 insert = 0;
    u32 i;
    for (i = 0; i < ds_elems; ++i) {
        data_set[insert] = malloc(sizeof(training_example));

        if (data_set[insert] == NULL) {
            flog_crit("dset", "system out of memory (1)\n");
        }

        r = fread(data_set[insert], sizeof(training_example), 1, fp);
        assert(r == 1);
        u32 base_insert = insert;
        ++insert;

        /*
        Generate more (0-7) cases from reduced ones
        */
        board tmp;
        for (d8 r = 2; r < 9; ++r) {
            memcpy(&tmp.p, &data_set[base_insert]->p, TOTAL_BOARD_SIZ);
            tmp.last_played = tmp.last_eaten = NONE;
            reduce_fixed(&tmp, r);

            bool repeated = false;
            for (u32 j = base_insert; j < insert; ++j) {
                if (memcmp(&tmp.p, &data_set[j]->p, TOTAL_BOARD_SIZ) == 0) {
                    repeated = true;
                    break;
                }
            }

            if (repeated) {
                continue;
            }

            data_set[insert] = malloc(sizeof(training_example));
            if (data_set[insert] == NULL) {
                flog_crit("dset", "system out of memory (2)\n");
            }

            memcpy(&data_set[insert]->p, &tmp.p, TOTAL_BOARD_SIZ);

            data_set[insert]->m = data_set[base_insert]->m;
            data_set[insert]->m = reduce_move(data_set[insert]->m, r);
            ++insert;
        }

        data_set_size = insert;
    }

    fclose(fp);

    data_set_shuffle_all();

    char * s = alloc();
    snprintf(s, MAX_PAGE_SIZ, "Data set loaded with %u examples, yielding %u examples\n", ds_elems, data_set_size);
    flog_info("dset", s);
    release(s);
    return data_set_size;
}

/*
Get a specific data set element by position.
*/
training_example * data_set_get(
    u32 pos
) {
    return data_set[pos];
}
