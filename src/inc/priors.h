/*
Heuristic UCT-RAVE implementation.

With RAVE and criticality.
Playout is limited with dynamic offset depending on stone count.
Cutoff playouts are rated. Playouts are cut short with a mercy threshold (like
pachi, orego and others).
Initilizes expanded states with prior values.
Last-good-reply with forgetting (LGRF1) is also used.
A virtual loss is also added on play traversion, that is later corrected if
needed.

MCTS can be resumed on demand by a few extra simulations at a time.
It can also record the average final score, for the purpose of score estimation.
*/

#ifndef MATILDA_PRIORS_H
#define MATILDA_PRIORS_H

#include "matilda.h"

#include "board.h"
#include "types.h"
#include "transpositions.h"
#include "cfg_board.h"

/*
Prior values heuristic contributions.
Set to 0 to disable each heuristic.
*/
#if BOARD_SIZ < 12
/*
Tuned with CLOP in 9x9 with 10k playouts/turn in self-play for 34k games.
*/
#define PRIOR_STONE_SCALE_FACTOR 1.28755
#define PRIOR_EVEN       31 /* even heuristic */
#define PRIOR_NAKADE     70 /* nakade heuristic */
#define PRIOR_SELF_ATARI 36 /* avoid self-ataris */
#define PRIOR_ATTACK     28
#define PRIOR_DEFEND     19
#define PRIOR_PAT3       23 /* 3x3 patterns centered on play */
#define PRIOR_NEAR_LAST  11 /* bonuses for distance to another stone */
#define PRIOR_LINE2      45 /* if empty in a certain distance around it */
#define PRIOR_LINE3      29
#define PRIOR_EMPTY      40 /* bonuses for empty zones of the board not above */
#define PRIOR_LINE1X     13 /* bonus for 3rd line and malus to 1st and 2nd */
#define PRIOR_LINE2X      5 /* if not empty in a certain distance around it */
#define PRIOR_LINE3X      8
#define PRIOR_CORNER     44
#define PRIOR_BAD_PLAY   95
#define PRIOR_PASS       130
#else
/*
Tuned based on 13x13 results, using CLOP in 13x13 with 10k playouts/turn vs
GNU Go 3.8 lvl 1, 720 matches.
*/
#define PRIOR_STONE_SCALE_FACTOR 1.34455
#define PRIOR_EVEN       12 /* even heuristic */
#define PRIOR_NAKADE     70 /* nakade heuristic */
#define PRIOR_SELF_ATARI 22 /* avoid self-ataris */
#define PRIOR_ATTACK     40
#define PRIOR_DEFEND     13
#define PRIOR_PAT3       22 /* 3x3 patterns centered on play */
#define PRIOR_NEAR_LAST   8 /* bonuses for distance to another stone */
#define PRIOR_LINE2      58 /* if empty in a certain distance around it */
#define PRIOR_LINE3      56
#define PRIOR_EMPTY      40 /* bonuses for empty zones of the board not above */
#define PRIOR_LINE1X      5 /* bonus for 3rd line and malus to 1st and 2nd */
#define PRIOR_LINE2X      5 /* if not empty in a certain distance around it */
#define PRIOR_LINE3X      6
#define PRIOR_CORNER     34
#define PRIOR_BAD_PLAY   96
#define PRIOR_PASS       87
#endif



/*
Initializes a game state structure with prior values and AMAF/LGRF/Criticality
information.
*/
void init_new_state(
    tt_stats * stats,
    cfg_board * cb,
    bool is_black,
    const bool branch_limit[TOTAL_BOARD_SIZ]
);

#if PRIOR_EVEN == 0
#error Error: MCTS prior weights: even heuristic weight cannot be zero.
#endif

#endif


