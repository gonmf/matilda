/*
Data set collection manipulation functions.

A data set file is defined as 4 bytes (unsigned int) indicating the
number of training cases, followed by the elements of struct
training_example type.

The examples are stored unique and invariant of flips and rotations, when
loaded via the data_set_load function they are fliped and rotated to increase
the data set size.
*/

#ifndef MATILDA_DATA_SET_H
#define MATILDA_DATA_SET_H

#include "matilda.h"

#include "types.h"
#include "move.h"

typedef struct __training_example_ {
    u8 p[TOTAL_BOARD_SIZ];
    move m;
} training_example;


/*
Shuffle all first num entries.
Fisher–Yates shuffle.
*/
void data_set_shuffle(
    u32 num
);

/*
Shuffle all data set.
Fisher–Yates shuffle.
*/
void data_set_shuffle_all();

/*
Read a data set and shuffle it.
RETURNS table set size (number of cases)
*/
u32 data_set_load();

/*
Read a data set, with a maximum size, and shuffles it.
RETURNS table set size (number of cases)
*/
u32 data_set_load2(
    u32 max
);

/*
Get a specific data set element by position.
*/
training_example * data_set_get(
    u32 pos
);

#endif
