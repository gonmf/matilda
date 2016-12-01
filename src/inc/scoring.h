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
Produces a textual representation of a Go match score., ex: B+3.5, 0
*/
void score_to_string(
    char * dst,
    d16 score
);

/*
Produces a textual representation of a komidashi value.
*/
void komi_to_string(
    char * dst,
    d16 komi
);

/*
Scoring by counting stones on the board only.
RETURNS positive score for a black win; negative for a white win; 0 for a draw
*/
d16 score_stones_only(
    const u8 p[TOTAL_BOARD_SIZ]
);

/*
Scoring by counting stones and eyes on the board only.
RETURNS positive score for a black win; negative for a white win; 0 for a draw
*/
d16 score_stones_and_eyes2(
    const cfg_board * cb
);

/*
Scoring by counting stones and eyes on the board only.
RETURNS positive score for a black win; negative for a white win; 0 for a draw
*/
d16 score_stones_and_eyes(
    const board * b
);

/*
Scoring by counting stones and surrounded area. Also known as area scoring. Does
not remove dead stones.
RETURNS positive score for a black win; negative for a white win; 0 for a draw
*/
d16 score_stones_and_area(
    const u8 p[TOTAL_BOARD_SIZ]
);

#endif
