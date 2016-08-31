/*
Heavy playout implementation with probability distribution selection and the use
of a play status cache.

The move selection policy uses the following restrictions:
    1. No illegal plays
    2. No playing in own proper eyes
    3. No plays ending in self-atari except if forming a single stone group
    (throw-in)
And chooses a play based on (by order of importance):
    1. Nakade
    2. Capture
    3. Avoid capture
    4. Handcrafted 3x3 patterns
    5. Random play
*/

#ifndef MATILDA_PLAYOUT_H
#define MATILDA_PLAYOUT_H

#include "matilda.h"

#include "types.h"
#include "cfg_board.h"


#define MAX_PLAYOUT_DEPTH_OVER_EMPTY (TOTAL_BOARD_SIZ / 3)

#define MERCY_THRESHOLD (TOTAL_BOARD_SIZ / 5)


/*
Probability of skipping a check in parts of 128 (instead of 100	for performance
reasons).
*/
#if BOARD_SIZ < 12
/*
Tuned with CLOP in 9x9 with 1k playouts/turn vs GNU Go 3.8 lvl 1, 8995 matches.
*/
#define PL_SKIP_SAVING  40
#define PL_SKIP_NAKADE  22
#define PL_SKIP_PATTERN  8
#define PL_SKIP_CAPTURE 72
#else
/*
Tuned with CLOP in 13x13 with 10k playouts/turn vs GNU Go 3.8 lvl 1. 2151 games.
*/
#define PL_SKIP_SAVING  22
#define PL_SKIP_NAKADE  69
#define PL_SKIP_PATTERN 23
#define PL_SKIP_CAPTURE 24
#endif


/*
Cache state bits (must fit in 1 byte)
*/
#define CACHE_PLAY_DIRTY 1 /* play needs to be recalculated */
#define CACHE_PLAY_LEGAL 2 /* if play is legal for that player */
#define CACHE_PLAY_OSAFE 4 /* if has 2 or more liberties after playing */




/*
Make a heavy playout and returns whether black wins.
Does not play in own proper eyes or self-ataris except for possible throw-ins.
Avoids too many ko battles. Also uses mercy threshold.
Also updates AMAF transitions information.
RETURNS the final score
*/
d16 playout_heavy_amaf(
    cfg_board * cb,
    bool is_black,
    u8 traversed[TOTAL_BOARD_SIZ]
);


#endif


