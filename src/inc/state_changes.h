/*
For operations on a Go state like placing a stone, passing, etc.
Where performance is important prefer using cfg_board structure and related
functions (cfg_board.h and cfg_board.c).
*/

#ifndef MATILDA_STATE_CHANGES_H
#define MATILDA_STATE_CHANGES_H

#include "matilda.h"

#include "types.h"
#include "board.h"



/*
Recursively counts the liberties after playing.
RETURNS liberties after playing regardless if play is legal
*/
u8 libs_after_play_slow(
    const board * b,
    bool is_black,
    move m,
    u16 * caps
);

/*
Recursively counts the liberties of a group.
RETURNS liberties of the group
*/
u8 slow_liberty_count(
    const board * b,
    move m
);

/*
Note: testing the last eaten position is not enough because current play might
be a multiple stone capture, thus not subject to ko rule.
RETURNS true if ko detected and play is invalid
*/
bool test_ko(
    board * b,
    move m,
    u8 own_stone /* attention */
);

/*
Performs a pass, updating the necessary information.
*/
void pass(
    board * b
);

/*
Plays ignoring if it is legal.
*/
void just_play_slow(
    board * b,
    bool is_black,
    move m
);

/*
Plays ignoring if it is legal.
*/
void just_play_slow2(
    board * b,
    bool is_black,
    move m,
    u16 * captured
);

/*
Plays ignoring if it is legal.
Also updates an associated Zobrist hash of the previous state.
RETURNS updated Zobrist hash
*/
u64 just_play_slow_and_get_hash(
    board * b,
    bool is_black,
    move m,
    u64 zobrist_hash
);

/*
Attempts to play, testing if it is legal.
If play is illegal original board is not changed.
RETURNS true if play was successful
*/
bool attempt_play_slow(
    board * b,
    bool is_black,
    move m
);

/*
Tests if play is valid disregarding superko rule.
Does not change original board.
RETURNS true if play is apparently legal
*/
bool can_play_slow(
    board * b,
    bool is_black,
    move m
);


#endif
