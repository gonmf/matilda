/*
Concept of move and related functions.

A move is either a stone play on the board or a pass.
A value of "none" can also be expressed for situations where a move is simply
absent, like the previous play in a new game.

A move type uses the constants PASS and NONE, and can be converted to (x, y)
coordinates (abbr. coord).

If instead using coordinates in the form (x, y), a value of x larger or equal to
BOARD_SIZ signifies a pass. A "none" play is not represented.
*/

#ifndef MATILDA_MOVE_H
#define MATILDA_MOVE_H

#include "config.h"

#include "types.h"


#if BOARD_SIZ < 16
typedef u8 move;
#else
typedef u16 move;
#endif

typedef struct __move_seq_ {
    move count;
    move coord[TOTAL_BOARD_SIZ];
} move_seq;

/*
Special move values
NONE is used when there is no information, like at the first turn of a match.
PASS is used on intended passes and signifies an intention to end the match.
*/
#define NONE TOTAL_BOARD_SIZ
#define PASS (TOTAL_BOARD_SIZ + 1)


/*
Move value changes for an adjacent intersection.
*/
#define TOP     (-BOARD_SIZ)
#define BOTTOM  BOARD_SIZ
#define LEFT    (-1)
#define RIGHT   1



/*
RETURNS true if move is a stone play
*/
bool is_board_move(
    move m
);

/*
Converts a (x,y) representation into a move.
Warning: Ignores passes
RETURNS move
*/
move coord_to_move(
    u8 x,
    u8 y
);

/*
Converts a move representation into a (x,y) one.
*/
void move_to_coord(
    move m,
    u8 * restrict x,
    u8 * restrict y
);

/*
RETURNS the Manhattan distance between two points
*/
u8 coord_distance(
    const u8 p1[static 2],
    const u8 p2[static 2]
);

/*
RETURNS the Manhattan distance between two points
*/
u8 move_distance(
    move a,
    move b
);

/*
Produces the move correspondent in the transformed matrix.
RETURNS move
*/
move reduce_move(
    move m,
    d8 method
);

/*
Parses a string for a move value, in the format D4.
The value I is skipped.
RETURNS move
*/
move coord_parse_alpha_num(
    const char * s
);

/*
Parses a string for a move value, in the format DE.
The character I is allowed.
RETURNS move
*/
move coord_parse_alpha_alpha(
    const char * s
);

/*
Parses a string for a move value, in the format 4-4.
RETURNS move
*/
move coord_parse_num_num(
    const char * s
);

/*
Converts a move to a string representation, like 4-4.
*/
void coord_to_num_num(
    char * dst,
    move m
);

/*
Converts a move to a string representation, like D4.
The value I is skipped.
*/
void coord_to_alpha_num(
    char * dst,
    move m
);

/*
Converts a move to a string representation, like DD.
The character I is allowed.
*/
void coord_to_alpha_alpha(
    char * dst,
    move m
);

/*
Populates a move_seq structure with the moves of distance equal or closer to
distance, for every intersection of a board.
*/
void init_moves_by_distance(
    move_seq neighbours[static TOTAL_BOARD_SIZ],
    u16 distance,
    bool include_own
);

/*
Copies the information from one move_seq structure, appending it at the tail of
another
*/
void copy_moves(
    move_seq * restrict dst,
    const move_seq * restrict src
);

/*
Add move to a move sequence structure.
Does not test if move is not already present.
*/
void add_move(
    move_seq * ms,
    move m
);

/*
Remove the given move from the move sequence structure.
Crashes if the move is not found.
*/
void rem_move(
    move_seq * ms,
    move m
);

#endif
