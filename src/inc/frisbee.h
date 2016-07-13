/*
Support for Frisbee go play random shifts.
*/


#ifndef MATILDA_FRISBEE_H
#define MATILDA_FRISBEE_H

#include "matilda.h"

#include "types.h"
#include "move.h"
#include "board.h"


/*
Only shift a board move.
RETURNS shifted board move or NONE if out of bounds
*/
move random_shift_play(
    move m
);

/*
Apply Frisbee Go transformation to move.
Assumes the original play is legal, but does not guarantee the modified play is
legal.
RETURNS previous, modified move or NONE is illegal.
*/
move frisbee_divert_play(
    board * b,
    bool is_black,
    move m,
    float accuracy
);


#endif
