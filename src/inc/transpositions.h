/*
Transpositions table and tree implementation.

Doesn't assume states are in reduced form. States contain full information and
are compared after the hash (collisions are impossible). Zobrist hashing with 64
bits is used. Clean-up is available only between turns or between games.

Please note there is no separate 'UCT state information' file. It is mostly
interweaved with the transpositions table.

The table is actually two tables, one for each player. Mixing their statistics
is illegal. The nodes statistics are from the perspective of the respective
table color.
*/

#ifndef MATILDA_TRANSPOSITIONS_H
#define MATILDA_TRANSPOSITIONS_H

#include "matilda.h"

#include <omp.h>

#include "mcts.h"
#include "board.h"
#include "cfg_board.h"
#include "move.h"
#include "types.h"

/*
Maximum number of plays in a board. Pass is not included because it is only
allowed when there are few plays possible.
*/
#define MAX_PLAYS_COUNT TOTAL_BOARD_SIZ

typedef struct __tt_play_ {
	move m;
	u32 mc_n;
	u32 amaf_n;
	double mc_q;
	double amaf_q;
	/* Criticality */
	double owner_winning;
	double color_owning;
	void * next_stats;
	struct __tt_play_ * lgrf1_reply;
} tt_play;


typedef struct __tt_stats_ {
	u64 zobrist_hash;
	u8 p[TOTAL_BOARD_SIZ];
    move last_eaten_passed; // position of last single stone eaten or NONE/PASS
	u8 maintenance_mark;
	d8 expansion_delay;
	move plays_count;
	tt_play plays[MAX_PLAYS_COUNT];
	omp_lock_t lock;
	struct __tt_stats_ * next;
} tt_stats;


/*
Initialize the transpositions table structures.
*/
void tt_init();

/*
Looks up a previously stored state, or generates a new one. No assumptions are
made about whether the board state is in reduced form already. Never fails. If
the memory is full it allocates a new state regardless. If the state is found
and returned it's OpenMP lock is first set. Thread-safe.
RETURNS the state information
*/
tt_stats * tt_lookup_create(
    const board * b,
    bool is_black,
    u64 hash
);

/*
Looks up a previously stored state, or generates a new one. No assumptions are
made about whether the board state is in reduced form already. If the limit on
states has been met the function returns NULL. If the state is found and
returned it's OpenMP lock is first set. Thread-safe.
RETURNS the state information or NULL
*/
tt_stats * tt_lookup_null(
    const cfg_board * cb,
    bool is_black,
    u64 hash
);

/*
Frees states outside of the subtree started at state b. Not thread-safe.
RETURNS number of states freed.
*/
u32 tt_clean_unreachable(
    const board * b,
    bool is_black
);

/*
Frees all game states and resets counters.
*/
u32 tt_clean_all();

/*
Mostly for debugging -- log the current memory status of the transpositions
table to stderr and log file.
*/
void tt_log_status();


#endif
