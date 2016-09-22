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

#ifndef MATILDA_MCTS_H
#define MATILDA_MCTS_H

#include "matilda.h"

#include "board.h"
#include "types.h"

/*
When to resign instead of insisting on playing.
*/
#define UCT_RESIGN_PLAYOUTS 100
#define UCT_RESIGN_WINRATE 0.10

/*
When to stop the search early because it is already very overwhelmingly
positive.
*/
#define UCT_CAN_STOP_EARLY true
#define UCT_EARLY_WINRATE 0.95

/*
How overwhelming a pass quality must be to be played even if not the top ranked.
*/
#define JUST_PASS_WINRATE 0.92


#define USE_UCT_BRANCH_LIMITER true

#define MAX_UCT_DEPTH ((TOTAL_BOARD_SIZ * 2) / 3)



/*
Performs a MCTS in at least the available time.

The search may end early if the estimated win rate is very one sided, in which
case the play selected is a pass. The search is also interrupted if memory runs
out.
RETURNS true if a play or pass is suggested instead of resigning
*/
bool mcts_start_timed(
    out_board * out_b,
    const board * b,
    bool is_black,
    u64 stop_time,
    u64 early_stop_time
);

/*
Performs a MCTS for the selected number of simulations.

The search is interrupted if memory runs out.
RETURNS true if a play or pass is suggested instead of resigning
*/
bool mcts_start_sims(
    out_board * out_b,
    const board * b,
    bool is_black,
    u32 simulations
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

/*
Execute a 1 second MCTS and return the number of simulations ran.
RETURNS simulations number
*/
u32 mcts_benchmark();

#endif


