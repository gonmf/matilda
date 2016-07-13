/*
Generic matrix transformations
*/

#ifndef MATILDA_MATRIX_H
#define MATILDA_MATRIX_H

#include "matilda.h"

#include "types.h"

/*
Board rotations, flips and color inverts to support automatic state reduction.
DO NOT CHANGE THESE VALUES - THEY ARE ASSUMED IN MULTIPLE APPLICATIONS both for
state reductions and matrix transpositions in general
*/
#define NOREDUCE   1
#define ROTATE90   2
#define ROTATE180  3
#define ROTATE270  4
#define ROTFLIP0   5
#define ROTFLIP90  6
#define ROTFLIP180 7
#define ROTFLIP270 8


/*
Rotate square matrix.
*/
void matrix_rotate(
    u8 * dst,
    const u8 * src,
    u16 side_len,
    u8 rotations
);

/*
Rotate contents of an out_board structure.
*/
void matrix_rotate2(
    out_board * dst,
    const out_board * src,
    u8 rotations
);

/*
Flips a square matrix.
*/
void matrix_flip(
    u8 * dst,
    const u8 * src,
    u16 side_len
);

/*
Flips the board contents of an out_board structure.
*/
void matrix_flip2(
    out_board * dst,
    const out_board * src
);

/*
Produces the move correspondent in the transformed matrix.
*/
void reduce_coord(
    u8 * x,
    u8 * y,
    u16 side_len,
    s8 method
);

#endif

