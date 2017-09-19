/*
Generic operations on a simple go board, like rotating, flipping, inverting
colors, counting stones, etc. This is complemented by the files board_io.c and
state_changes.c.

More board functions related to cleaning and outputing board states.

For a more advanced board representation check the CFG representation
(cfg_board.c and cfg_board.h).
*/

#ifndef MATILDA_BOARD_H
#define MATILDA_BOARD_H

#include "config.h"

#include <unistd.h>
#include <stdio.h>

#include "types.h"
#include "move.h"

/*
Simple board definition
Warning: don't change the order of the fields
*/
typedef struct __board_ {
    u8 p[TOTAL_BOARD_SIZ];
    move last_eaten;
    move last_played;
} board;

/*
For displaying Go boards

Notice GTP uses European notation so it may be confusing to debug the GTP
interface using Japanese notation.
*/
#define EMPTY_STONE_CHAR     '.'
#define BLACK_STONE_CHAR     'X'
#define WHITE_STONE_CHAR     'O'
#define BLACK_STONE_CHAR_ALT 'x'
#define WHITE_STONE_CHAR_ALT 'o'

#define EUROPEAN_NOTATION true

/*
WARNING: DO NOT CHANGE
1. many functions assume these values are fixed
2. EMPTY should be 0 for performing faster bzeros
*/
#define EMPTY       0
#define BLACK_STONE 1
#define WHITE_STONE 2
#define ILLEGAL     3 /* seldom used */

typedef struct __out_board_ {
    double value[TOTAL_BOARD_SIZ];
    bool tested[TOTAL_BOARD_SIZ];
    double pass;
} out_board;


#define DISTANCE_TO_BORDER(X,Y) \
    MIN((X), MIN((Y), MIN(BOARD_SIZ - 1 - (X), BOARD_SIZ - 1 - (Y))))


/*
Number of bytes needed to store SxS positions, 2 bits per position
*/
#define PACKED_BOARD_SIZ (TOTAL_BOARD_SIZ / 4 + 1)



/*
Converts a 1 byte per position representation into a 2 bit per position
representation.
*/
void pack_matrix(
    u8 dst[PACKED_BOARD_SIZ],
    const u8 src[TOTAL_BOARD_SIZ]
);

/*
Converts a 2 bit per position representation into a 1 byte per position
representation.
*/
void unpack_matrix(
    u8 dst[TOTAL_BOARD_SIZ],
    const u8 src[PACKED_BOARD_SIZ]
);

/*
Tests if two non-overlapping board structures have the same content.
RETURNS true if equal
*/
bool board_are_equal(
    board * restrict a,
    const board * restrict b
);

/*
Counts the number of non-empty intersections on the board.
RETURNS stone count
*/
u16 stone_count(
    const u8 p[TOTAL_BOARD_SIZ]
);

/*
Counts the difference in the number of black and white stones on the board.
RETURNS difference in stone numbers, positive values for more black stones
*/
d16 stone_diff(
    const u8 p[TOTAL_BOARD_SIZ]
);

/*
Counts the number of stones and difference between the stones colors.
count is affected with the stone count and diff is affected with the difference
in stone colors (positive values if more black stones).
*/
void stone_count_and_diff(
    const u8 p[TOTAL_BOARD_SIZ],
    u16 * count, d16 * diff
);

/*
Inverts the color of the stones on the board.
*/
void invert_color(
    u8 p[TOTAL_BOARD_SIZ]
);

/*
Flips and rotates the board contents to produce a unique representative. Also
updates the last eaten/played values.
Also inverts the color if is_black is false.
RETURNS reduction method that can be used to revert the reduction
*/
d8 reduce_auto(
    board * b,
    bool is_black
);

/*
Modifies the board according to a reduction method.
*/
void reduce_fixed(
    board * b,
    d8 method
);

/*
Performs the inverse operation of reduction of a given reduce code.
*/
void oboard_revert_reduce(
    out_board * b,
    d8 method
);

/*
Clears the contents of a board.
*/
void clear_board(
    board * b
);

/*
Clears the contents of an output board.
*/
void clear_out_board(
    out_board * b
);

/*
Format a string with a representation of the contents of an output board.
RETURNS string representation
*/
void out_board_to_string(
    char * dst,
    const out_board * src
);

/*
Prints the string representation on an output board.
*/
void fprint_out_board(
    FILE * fp,
    const out_board * b
);

/*
Format a string with a representation of the contents of a board, complete with
ko violation indication and subject to the display options of european/japanese
styles (defined in board.h).
RETURNS string representation
*/
void board_to_string(
    char * dst,
    const u8 p[TOTAL_BOARD_SIZ],
    move last_played,
    move last_eaten
);

/*
Print a board string representation.
*/
void fprint_board(
    FILE * fp,
    const board * b
);

#endif
