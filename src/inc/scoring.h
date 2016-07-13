/*
Functions for board scoring that take komi and dynamic komi into consideration.

Remember that in Matilda, scores and komi are always doubled to become integer.
*/

#ifndef MATILDA_SCORING_H
#define MATILDA_SCORING_H

#include "matilda.h"

#include "types.h"
#include "board.h"
#include "cfg_board.h"


/*
Returns a statically allocated string with the textual representation of a Go
match score.
RETURNS string with score
*/
const char * score_to_string(
    s16 score
);

/*
Returns a statically allocated string with the textual representation of a
komidashi value.
RETURNS string with komi
*/
const char * komi_to_string(
    s16 komi
);

/*
Scoring by counting stones on the board only.
RETURNS positive score for a black win; negative for a white win; 0 for a draw
*/
s16 score_stones_only(
    const u8 p[BOARD_SIZ * BOARD_SIZ]
);

/*
Scoring by counting stones and eyes on the board only.
RETURNS positive score for a black win; negative for a white win; 0 for a draw
*/
s16 score_stones_and_eyes2(
    const cfg_board * cb
);

/*
Scoring by counting stones and eyes on the board only.
RETURNS positive score for a black win; negative for a white win; 0 for a draw
*/
s16 score_stones_and_eyes(
    const board * b
);

/*
Scoring by counting stones and surrounded area. Also known as area scoring. Does
not remove dead stones.
RETURNS positive score for a black win; negative for a white win; 0 for a draw
*/
s16 score_stones_and_area(
    const u8 p[BOARD_SIZ * BOARD_SIZ]
);

/*
Scoring by estimating the final status of each intersection by running MCTS.
This method is more accurate and fit for any part of the game, but much slower.
Each intersection is awarded the player that has a majority of stones there in
the end. The simulations ignore superkos. After simulating the final result,
area scoring is used.
RETURNS positive score for a black win; negative for a white win; 0 for a draw
*/
s16 score_estimate(
    const board * b,
    bool is_black
);

/*
Estimate the final game position from the current state. Is the most accurate
the later in the game.
*/
void estimate_final_position(
    const board * b,
    bool is_black,
    u8 e[BOARD_SIZ * BOARD_SIZ]
);

/*
Attempts to identify and remove dead groups.
*/
void remove_dead_stones(
    board * b
);


#endif
