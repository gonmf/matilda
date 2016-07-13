/*
Generic operations on a simple go board, like rotating, flipping, inverting
colors, counting stones, etc. This is complemented by the files board_io.c and
state_changes.c.

For a more advanced board representation check the CFG representation
(cfg_board.c and cfg_board.h).
*/

#include "matilda.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "types.h"
#include "board.h"
#include "move.h"
#include "matrix.h"

s16 komi = DEFAULT_KOMI;



/*
Converts a 1 byte per position representation into a 2 bit per position
representation.
*/
void pack_matrix(
    const u8 src[BOARD_SIZ * BOARD_SIZ],
    u8 dst[PACKED_BOARD_SIZ]
){
    memset(dst, 0, PACKED_BOARD_SIZ);
    for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
        dst[m / 4] |= src[m] << ((m % 4) * 2);
}

/*
Converts a 2 bit per position representation into a 1 byte per position
representation.
*/
void unpack_matrix(
    u8 dst[BOARD_SIZ * BOARD_SIZ],
    const u8 src[PACKED_BOARD_SIZ]
){
    for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
        dst[m] = (src[m / 4] >> ((m % 4) * 2)) & 0x3;
}


/*
Tests if two non-overlapping board structures have the same content.
RETURNS true if equal
*/
bool board_are_equal(
    board * restrict a,
    const board * restrict b
){
    return memcmp(a->p, b->p, BOARD_SIZ * BOARD_SIZ) == 0 && a->last_played ==
        b->last_played && a->last_eaten == b->last_eaten;
}


/*
Counts the number of non-empty intersections on the board.
RETURNS stone count
*/
u16 stone_count(
    const u8 p[BOARD_SIZ * BOARD_SIZ]
){
    u16 count = 0;
    for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
        if(p[m] != EMPTY)
            ++count;
    return count;
}

/*
Counts the difference in the number of black and white stones on the board.
RETURNS difference in stone numbers, positive values for more black stones
*/
s16 stone_diff(
    const u8 p[BOARD_SIZ * BOARD_SIZ]
){
    s16 diff = 0;
    for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
        if(p[m] == BLACK_STONE)
            ++diff;
        else
            if(p[m] == WHITE_STONE)
                --diff;
    return diff;
}

/*
Counts the number of stones and difference between the stones colors.
count is affected with the stone count and diff is affected with the difference
in stone colors (positive values if more black stones).
*/
void stone_count_and_diff(
    const u8 p[BOARD_SIZ * BOARD_SIZ],
    u16 * count, s16 * diff
){
    s16 d = 0;
    u16 c = 0;
    for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
        if(p[m] == BLACK_STONE)
        {
            ++d;
            ++c;
        }
        else
            if(p[m] == WHITE_STONE){
                --d;
                ++c;
            }
    *count = c;
    *diff = d;
}

/*
Inverts the color of the stones on the board.
*/
void invert_color(
    u8 p[BOARD_SIZ * BOARD_SIZ]
){
    for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
        if(p[m] == BLACK_STONE)
            p[m] = WHITE_STONE;
        else
            if(p[m] == WHITE_STONE)
                p[m] = BLACK_STONE;
}

/*
Flips and rotates the board contents to produce a unique representative. Also
updates the last eaten/played values.
Also inverts the color if is_black is false.
RETURNS reduction method that can be used to revert the reduction
*/
s8 reduce_auto(
    board * b,
    bool is_black
){
    if(!is_black)
        invert_color(b->p);

    u8 r1[BOARD_SIZ * BOARD_SIZ];
    u8 r2[BOARD_SIZ * BOARD_SIZ];
    u8 r3[BOARD_SIZ * BOARD_SIZ];
    matrix_rotate(r1, b->p, BOARD_SIZ, 1);
    matrix_rotate(r2, b->p, BOARD_SIZ, 2);
    matrix_rotate(r3, b->p, BOARD_SIZ, 3);
    u8 f0[BOARD_SIZ * BOARD_SIZ];
    u8 f1[BOARD_SIZ * BOARD_SIZ];
    u8 f2[BOARD_SIZ * BOARD_SIZ];
    u8 f3[BOARD_SIZ * BOARD_SIZ];
    matrix_flip(f0, b->p, BOARD_SIZ);
    matrix_flip(f1, r1, BOARD_SIZ);
    matrix_flip(f2, r2, BOARD_SIZ);
    matrix_flip(f3, r3, BOARD_SIZ);

    s8 reduction = NOREDUCE;
    void * champion = b->p;
    if(memcmp(champion, r1, BOARD_SIZ * BOARD_SIZ) > 0)
    {
        champion = r1;
        reduction = ROTATE90;
    }

    if(memcmp(champion, r2, BOARD_SIZ * BOARD_SIZ) > 0)
    {
        champion = r2;
        reduction = ROTATE180;
    }

    if(memcmp(champion, r3, BOARD_SIZ * BOARD_SIZ) > 0)
    {
        champion = r3;
        reduction = ROTATE270;
    }

    if(memcmp(champion, f0, BOARD_SIZ * BOARD_SIZ) > 0)
    {
        champion = f0;
        reduction = ROTFLIP0;
    }

    if(memcmp(champion, f1, BOARD_SIZ * BOARD_SIZ) > 0)
    {
        champion = f1;
        reduction = ROTFLIP90;
    }

    if(memcmp(champion, f2, BOARD_SIZ * BOARD_SIZ) > 0)
    {
        champion = f2;
        reduction = ROTFLIP180;
    }

    if(memcmp(champion, f3, BOARD_SIZ * BOARD_SIZ) > 0)
    {
        champion = f3;
        reduction = ROTFLIP270;
    }

    switch(reduction)
    {
        case ROTATE90:
            memcpy(b->p, r1, BOARD_SIZ * BOARD_SIZ);
            break;
        case ROTATE180:
            memcpy(b->p, r2, BOARD_SIZ * BOARD_SIZ);
            break;
        case ROTATE270:
            memcpy(b->p, r3, BOARD_SIZ * BOARD_SIZ);
            break;
        case ROTFLIP0:
            memcpy(b->p, f0, BOARD_SIZ * BOARD_SIZ);
            break;
        case ROTFLIP90:
            memcpy(b->p, f1, BOARD_SIZ * BOARD_SIZ);
            break;
        case ROTFLIP180:
            memcpy(b->p, f2, BOARD_SIZ * BOARD_SIZ);
            break;
        case ROTFLIP270:
            memcpy(b->p, f3, BOARD_SIZ * BOARD_SIZ);
            break;
    }

    b->last_played = reduce_move(b->last_played, reduction);
    b->last_eaten = reduce_move(b->last_eaten, reduction);

    return is_black ? reduction : -reduction;
}

/*
Modifies the board according to a reduction method.
*/
void reduce_fixed(
    board * b,
    s8 method
){
    if(method < 0)
    {
        invert_color(b->p);
        method = method * -1;
    }

    if(method == NOREDUCE)
        return;

    u8 r[BOARD_SIZ * BOARD_SIZ];
    u8 f[BOARD_SIZ * BOARD_SIZ];
    switch(method)
    {
        case ROTATE90:
            matrix_rotate(r, b->p, BOARD_SIZ, 1);
            break;
        case ROTATE180:
            matrix_rotate(r, b->p, BOARD_SIZ, 2);
            break;
        case ROTATE270:
            matrix_rotate(r, b->p, BOARD_SIZ, 3);
            break;
        case ROTFLIP0:
            matrix_flip(r, b->p, BOARD_SIZ);
            break;
        case ROTFLIP90:
            matrix_rotate(f, b->p, BOARD_SIZ, 1);
            matrix_flip(r, f, BOARD_SIZ);
            break;
        case ROTFLIP180:
            matrix_rotate(f, b->p, BOARD_SIZ, 2);
            matrix_flip(r, f, BOARD_SIZ);
            break;
        case ROTFLIP270:
            matrix_rotate(f, b->p, BOARD_SIZ, 3);
            matrix_flip(r, f, BOARD_SIZ);
            break;
    }
    memcpy(b->p, r, BOARD_SIZ * BOARD_SIZ);

    b->last_played = reduce_move(b->last_played, method);
    b->last_eaten = reduce_move(b->last_eaten, method);
}

/*
Performs the inverse operation of reduction of a given reduce code.
*/
void oboard_revert_reduce(
    out_board * b,
    s8 method
){
    if(method < 0)
        method = method * -1;

    out_board r;
    out_board f;
    switch(method)
    {
        case ROTATE90:
            matrix_rotate2(&r, b, 3);
            break;
        case ROTATE180:
            matrix_rotate2(&r, b, 2);
            break;
        case ROTATE270:
            matrix_rotate2(&r, b, 1);
            break;
        case ROTFLIP0:
            matrix_flip2(&r, b);
            break;
        case ROTFLIP90:
            matrix_flip2(&f, b);
            matrix_rotate2(&r, &f, 3);
            break;
        case ROTFLIP180:
            matrix_flip2(&f, b);
            matrix_rotate2(&r, &f, 2);
            break;
        case ROTFLIP270:
            matrix_flip2(&f, b);
            matrix_rotate2(&r, &f, 1);
            break;
        default:
            return;
    }
    memcpy(b, &r, sizeof(out_board));
}


