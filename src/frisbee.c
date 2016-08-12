/*
Support for Frisbee go play random shifts.
*/


#include "matilda.h"

#include "types.h"
#include "board.h"
#include "move.h"
#include "state_changes.h"
#include "board.h"
#include "randg.h"


extern bool border_left[TOTAL_BOARD_SIZ];
extern bool border_right[TOTAL_BOARD_SIZ];
extern bool border_top[TOTAL_BOARD_SIZ];
extern bool border_bottom[TOTAL_BOARD_SIZ];

/*
Only shift a board move.
RETURNS shifted board move or NONE if out of bounds
*/
move random_shift_play(
    move m
){
    move n;
    switch(rand_u16(4))
    {
        case 0:
            if(!border_left[m])
                n = m + LEFT;
            else
                n = NONE;
            break;
        case 1:
            if(!border_right[m])
                n = m + RIGHT;
            else
                n = NONE;
            break;
        case 2:
            if(!border_top[m])
                n = m + TOP;
            else
                n = NONE;
            break;
        default:
            if(!border_bottom[m])
                n = m + BOTTOM;
            else
                n = NONE;
    }
    return n;
}


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
){
    if(!is_board_move(m))
        return m;

    if(accuracy == 1.0 || rand_float(1.0) >= accuracy)
        return m;

    move n = random_shift_play(m);

    if(!is_board_move(n))
        return NONE;

    if(b->p[n] != EMPTY)
        return NONE;

    if(!can_play_slow(b, n, is_black))
        return NONE;

    return n;
}

