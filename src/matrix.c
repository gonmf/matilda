/*
Generic matrix transformations
*/

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "board.h"
#include "matrix.h"
#include "move.h"
#include "types.h"


/*
Rotate square matrix.
*/
void matrix_rotate(
    u8 dst[static TOTAL_BOARD_SIZ],
    const u8 src[static TOTAL_BOARD_SIZ],
    u16 side_len,
    u8 rotations
) {
    assert(rotations < 4);

    u8 x;
    u8 y;

    switch (rotations) {
        case 0:
            memcpy(dst, src, side_len * side_len);
            break;
        case 1:
            --side_len;
            for (move m = 0; m < TOTAL_BOARD_SIZ; ++m)
            {
                move_to_coord(m, &x, &y);
                dst[m] = src[coord_to_move(side_len - y, x)];
            }
            break;
        case 2:
            --side_len;
            for (move m = 0; m < TOTAL_BOARD_SIZ; ++m)
            {
                dst[m] = src[TOTAL_BOARD_SIZ - 1 - m];
            }
            break;
        case 3:
            --side_len;
            for (move m = 0; m < TOTAL_BOARD_SIZ; ++m)
            {
                move_to_coord(m, &x, &y);
                dst[m] = src[coord_to_move(y, side_len - x)];
            }
            break;
    }
}

/*
Rotate contents of an out_board structure.
*/
void matrix_rotate2(
    out_board * restrict dst,
    const out_board * restrict src,
    u8 rotations
) {
    assert(rotations < 4);

    u8 x;
    u8 y;

    switch (rotations) {
        case 0:
            memcpy(dst->value, src->value, TOTAL_BOARD_SIZ *
                sizeof(double));
            memcpy(dst->tested, src->tested, TOTAL_BOARD_SIZ);
            break;
        case 1:
            for (move m = 0; m < TOTAL_BOARD_SIZ; ++m)
            {
                move_to_coord(m, &x, &y);
                move n = coord_to_move(BOARD_SIZ - 1 - y, x);
                dst->value[m] = src->value[n];
                dst->tested[m] = src->tested[n];
            }
            break;
        case 2:
            for (move m = 0; m < TOTAL_BOARD_SIZ; ++m)
            {
                dst->value[m] = src->value[TOTAL_BOARD_SIZ - 1 - m];
                dst->tested[m] = src->tested[TOTAL_BOARD_SIZ - 1 - m];
            }
            break;
        case 3:
            for (move m = 0; m < TOTAL_BOARD_SIZ; ++m)
            {
                move_to_coord(m, &x, &y);
                move n = coord_to_move(y, BOARD_SIZ - 1 - x);
                dst->value[m] = src->value[n];
                dst->tested[m] = src->tested[n];
            }
            break;
    }

    dst->pass = src->pass;
}

/*
Flips a square matrix.
*/
void matrix_flip(
    u8 dst[static TOTAL_BOARD_SIZ],
    const u8 src[static TOTAL_BOARD_SIZ],
    u16 side_len
) {
    u8 x;
    u8 y;

    --side_len;
    for (move m = 0; m < TOTAL_BOARD_SIZ; ++m) {
        move_to_coord(m, &x, &y);
        dst[m] = src[coord_to_move(side_len - x, y)];
    }
}

/*
Flips the board contents of an out_board structure.
*/
void matrix_flip2(
    out_board * restrict dst,
    const out_board * restrict src
) {
    u8 x;
    u8 y;

    for (move m = 0; m < TOTAL_BOARD_SIZ; ++m) {
        move_to_coord(m, &x, &y);
        move n = coord_to_move(BOARD_SIZ - 1 - x, y);
        dst->value[m] = src->value[n];
        dst->tested[m] = src->tested[n];
    }

    dst->pass = src->pass;
}


/*
Produces the move correspondent in the transformed matrix.
*/
void reduce_coord(
    u8 * restrict x,
    u8 * restrict y,
    u16 side_len,
    d8 method
) {
    if (method == NOREDUCE || *x >= BOARD_SIZ)
        return;

    if (method < 0)
        method = method * -1;

    u8 ox = *x;
    u8 oy = *y;
    u8 ix;
    u8 iy;
    switch (method) {
        case ROTATE90:
            ox = *y;
            oy = side_len - 1 - *x;
            break;
        case ROTATE180:
            ox = side_len - 1 - *x;
            oy = side_len - 1 - *y;
            break;
        case ROTATE270:
            ox = side_len - 1 - *y;
            oy = *x;
            break;
        case ROTFLIP0:
            ox = side_len - 1 - *x;
            oy = *y;
            break;
        case ROTFLIP90:
            ix = *y;
            iy = side_len - 1 - *x;

            ox = side_len - 1 - ix;
            oy = iy;
            break;
        case ROTFLIP180:
            ix = side_len - 1 - *x;
            iy = side_len - 1 - *y;

            ox = side_len - 1 - ix;
            oy = iy;
            break;
        case ROTFLIP270:
            ix = side_len - 1 - *y;
            iy = *x;

            ox = side_len - 1 - ix;
            oy = iy;
            break;
    }

    *x = ox;
    *y = oy;
}


