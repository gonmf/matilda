/*
Heuristic UCT-RAVE implementation.

With UCB1-TUNED, RAVE and criticality.
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

#ifndef MATILDA_MCTS_H
#define MATILDA_MCTS_H

#include "matilda.h"

#include "board.h"
#include "types.h"

/*
Ranges outside which the MCTS UCT algorithm stops early.
*/
#define CAN_STOP_EARLY true
#define UCT_MIN_WINRATE 0.10
#define UCT_MAX_WINRATE 0.95

#define USE_UCT_BRANCH_LIMITER true

#define MAX_UCT_DEPTH ((BOARD_SIZ * BOARD_SIZ * 2) / 3)


/*
Constant used as coefficient of UCB contribution in UCT formula.
*/
#if BOARD_SIZ < 12
/*
Tuned with CLOP in 9x9 with 1k playouts/turn vs GNU Go 3.8 lvl 1. 4285 games.
*/
#define UCB1_C 0.867924
#else
/*
Tuned with CLOP in 13x13 with 10k playouts/turn vs GNU Go 3.8 lvl 1. 2151 games.
*/
#define UCB1_C 0.854524
#endif


/*
Prior values heuristic contributions.
Set to 0 to disable each heuristic.
*/
#if BOARD_SIZ < 12
/*
Tuned with CLOP in 9x9 with 1k playouts/turn vs GNU Go 3.8 lvl 1, 29179 matches.
*/
#define PRIOR_STONE_SCALE_FACTOR 1.24271
#define PRIOR_EVEN       13 /* even heuristic */
#define PRIOR_NAKADE     72 /* nakade heuristic */
#define PRIOR_SELF_ATARI 22 /* avoid self-ataris */
#define PRIOR_ATTACK     43
#define PRIOR_DEFEND     13
#define PRIOR_PAT3       21 /* 3x3 patterns centered on play */
#define PRIOR_NEAR_LAST   6 /* bonuses for distance to another stone */
#define PRIOR_LINE2      57 /* if empty in a certain distance around it */
#define PRIOR_LINE3      61
#define PRIOR_EMPTY      40 /* bonuses for empty zones of the board not above */
#define PRIOR_LINE1X      3 /* bonus for 3rd line and malus to 1st and 2nd */
#define PRIOR_LINE2X      4 /* if not empty in a certain distance around it */
#define PRIOR_LINE3X      3
#define PRIOR_CORNER     34
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
#endif



/*
Instruct MCTS to take not of final positions, for final score estimation. The
results are gathered and return when calling disable_estimate_score.
*/
void enable_estimate_score();

/*
Disable score estimation and return the number of times each position belonged
to each player color.
*/
void disable_estimate_score(
    u32 black_occupied[BOARD_SIZ * BOARD_SIZ],
    u32 white_occupied[BOARD_SIZ * BOARD_SIZ]
);

/*
Performs a MCTS in at least the available time.

The search may end early if the estimated win rate is very one sided, in which
case the play selected is a pass. The search is also interrupted if memory runs
out.
RETURNS the estimated probability of winning the match (ignoring passes)
*/
double mcts_start(
    out_board * out_b,
    const board * b,
    bool is_black,
    u64 stop_time,
    u64 early_stop_time
);

/*
Reset whether MCTS can run in the background after a previous attempt may have
run out of memory.
*/
void reset_mcts_can_resume();

/*
Continue a previous MCTS.
*/
void mcts_resume(
    const board * b,
    bool is_black
);




#if PRIOR_EVEN == 0
#error Error: MCTS prior weights: even heuristic weight cannot be zero.
#endif

#endif


