/*
Functions that deal with updating AMAF information and its use in MC-RAVE.

Uses a minimum MSE schedule.

AMAF traversions are marked EMPTY when not visited, BLACK_STONE for first
visited by black and WHITE_STONE for first visited by white.
*/

#ifndef MATILDA_AMAF_RAVE_H
#define MATILDA_AMAF_RAVE_H

#include "matilda.h"

#include "board.h"
#include "types.h"
#include "transpositions.h"

/*
Set to use AMAF/RAVE in MCTS-UCT.

EXPECTED: 0 or 1
*/
#define USE_AMAF_RAVE 1


/*
Constant b of the MSE schedule for RAVE from 'Monte-Carlo tree search and rapid
action estimation in computer Go' by Sylvain Gelly and David Silver
*/
#if BOARD_SIZ < 12
/*
Tuned with CLOP in 9x9 with 1k playouts/turn vs GNU Go 3.8 lvl 1. 37k games.
*/
#define RAVE_MSE_EQUIV 2003
#else
#define RAVE_MSE_EQUIV 2003
#endif


/*
Minimum visits to a state before taking its criticality into consideration.
Use 0 to turn off.
*/
#define CRITICALITY_THRESHOLD 450



/*
Calculation of the RAVE value of a state transition.
RETURNS overall value of play (x,y)
*/
double uct1_rave(
    const tt_play * play
);

/*
Batch update of all transitions that were visited anytime after the current
state (if visited first by the player).
*/
void update_amaf_stats(
    tt_stats * stats,
    const u8 traversed[TOTAL_BOARD_SIZ],
    bool is_black,
    double z
);

/*
Batch update of all transitions that were visited anytime after the current
state (if visited first by the player).
This versions only adds losses -- is meant to use when a draw occurs.
*/
void update_amaf_stats2(
    tt_stats * stats,
    const u8 traversed[TOTAL_BOARD_SIZ],
    bool is_black
);

#endif
