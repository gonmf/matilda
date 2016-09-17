/*
Initialization of game wide constants based on board size.

To use declare as external:
d16 komi;
u8 out_neighbors8[TOTAL_BOARD_SIZ];
u8 out_neighbors4[TOTAL_BOARD_SIZ];
move_seq neighbors_side[TOTAL_BOARD_SIZ];
move_seq neighbors_diag[TOTAL_BOARD_SIZ];
move_seq neighbors_3x3[TOTAL_BOARD_SIZ];
bool border_left[TOTAL_BOARD_SIZ];
bool border_right[TOTAL_BOARD_SIZ];
bool border_top[TOTAL_BOARD_SIZ];
bool border_bottom[TOTAL_BOARD_SIZ];
u8 distances_to_border[TOTAL_BOARD_SIZ];
move_seq nei_dst_3[TOTAL_BOARD_SIZ];
move_seq nei_dst_4[TOTAL_BOARD_SIZ];
u8 active_bits_in_byte[256];
*/

#include "matilda.h"

#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "flog.h"
#include "pat3.h"
#include "move.h"
#include "types.h"

d16 komi;
u8 out_neighbors8[TOTAL_BOARD_SIZ];
u8 out_neighbors4[TOTAL_BOARD_SIZ];
move_seq neighbors_side[TOTAL_BOARD_SIZ];
move_seq neighbors_diag[TOTAL_BOARD_SIZ];
move_seq neighbors_3x3[TOTAL_BOARD_SIZ];
bool border_left[TOTAL_BOARD_SIZ];
bool border_right[TOTAL_BOARD_SIZ];
bool border_top[TOTAL_BOARD_SIZ];
bool border_bottom[TOTAL_BOARD_SIZ];
u8 distances_to_border[TOTAL_BOARD_SIZ];
move_seq nei_dst_3[TOTAL_BOARD_SIZ];
move_seq nei_dst_4[TOTAL_BOARD_SIZ];

u8 active_bits_in_byte[256];
bool black_eye[65536];
bool white_eye[65536];

static bool board_constants_inited = false;

static u8 count_bits(u8 v)
{
    u8 bits = 0;
    while(v > 0)
    {
        if((v & 1) == 1)
            ++bits;
        v /= 2;
    }
    return bits;
}

/*
An eye is a point that may eventually become untakeable (without playing
at the empty intersection itself). Examples:

.bw   .b.   ---   +--
b*b   b*b   b*b   |*b
.bb   .bb   .b.   |b.
*/
static u8 _out_neighbors4(
    u8 p[3][3]
){
    u8 ret = 0;
    if(p[1][0] == ILLEGAL)
        ++ret;
    if(p[2][1] == ILLEGAL)
        ++ret;
    if(p[1][2] == ILLEGAL)
        ++ret;
    if(p[0][1] == ILLEGAL)
        ++ret;
    return ret;
}

static u8 _black_neighbors4(
    u8 p[3][3]
){
    u8 ret = 0;
    if(p[1][0] == BLACK_STONE)
        ++ret;
    if(p[2][1] == BLACK_STONE)
        ++ret;
    if(p[1][2] == BLACK_STONE)
        ++ret;
    if(p[0][1] == BLACK_STONE)
        ++ret;
    return ret;
}

static u8 _white_neighbors4(
    u8 p[3][3]
){
    u8 ret = 0;
    if(p[1][0] == WHITE_STONE)
        ++ret;
    if(p[2][1] == WHITE_STONE)
        ++ret;
    if(p[1][2] == WHITE_STONE)
        ++ret;
    if(p[0][1] == WHITE_STONE)
        ++ret;
    return ret;
}

static u8 _black_neighbors8(
    u8 p[3][3]
){
    u8 ret = _black_neighbors4(p);
    if(p[0][0] == BLACK_STONE)
        ++ret;
    if(p[2][0] == BLACK_STONE)
        ++ret;
    if(p[0][2] == BLACK_STONE)
        ++ret;
    if(p[2][2] == BLACK_STONE)
        ++ret;
    return ret;
}

static u8 _white_neighbors8(
    u8 p[3][3]
){
    u8 ret = _white_neighbors4(p);
    if(p[0][0] == WHITE_STONE)
        ++ret;
    if(p[2][0] == WHITE_STONE)
        ++ret;
    if(p[0][2] == WHITE_STONE)
        ++ret;
    if(p[2][2] == WHITE_STONE)
        ++ret;
    return ret;
}

static void init_eye_table()
{
    u8 dst[3][3];
    for(u32 i = 0; i < 65536; ++i)
    {
        string_to_pat3(dst, i);

        if(_out_neighbors4(dst) == 0)
        {
            black_eye[i] = (_black_neighbors4(dst) == 4) &&
                (_white_neighbors8(dst) < 2);
            white_eye[i] = (_white_neighbors4(dst) == 4) &&
                (_black_neighbors8(dst) < 2);
        }
        else
        {
            black_eye[i] = (_black_neighbors4(dst) + _out_neighbors4(dst) == 4)
                && (_white_neighbors8(dst) == 0);
            white_eye[i] = (_white_neighbors4(dst) + _out_neighbors4(dst) == 4)
                && (_black_neighbors8(dst) == 0);
        }
    }
}

/*
Initialize a series of constants based on the board size in use.
*/
void board_constants_init()
{
    if(board_constants_inited)
        return;
    board_constants_inited = true;

    komi = DEFAULT_KOMI;

    /* Adjacent neighbor positions */
    init_moves_by_distance(neighbors_side, 1, false);

    memset(border_left, false, TOTAL_BOARD_SIZ);
    memset(border_right, false, TOTAL_BOARD_SIZ);
    memset(border_top, false, TOTAL_BOARD_SIZ);
    memset(border_bottom, false, TOTAL_BOARD_SIZ);

    /* Diagonal neighbor positions */
    for(d8 x = 0; x < BOARD_SIZ; ++x)
        for(d8 y = 0; y < BOARD_SIZ; ++y)
        {
            move a = coord_to_move(x, y);
            if(x == 0)
                border_left[a] = true;
            if(x == BOARD_SIZ - 1)
                border_right[a] = true;
            if(y == 0)
                border_top[a] = true;
            if(y == BOARD_SIZ - 1)
                border_bottom[a] = true;

            move c = 0;
            for(d8 i = 0; i < BOARD_SIZ; ++i)
                for(d8 j = 0; j < BOARD_SIZ; ++j)
                    if(abs(x - i) == 1 && abs(y - j) == 1)
                    {
                        move b = coord_to_move(i, j);
                        neighbors_diag[a].coord[c] = b;
                        ++c;
                    }
            neighbors_diag[a].count = c;
        }

    /* 3x3 positions excluding self */
    memcpy(neighbors_3x3, neighbors_side, TOTAL_BOARD_SIZ *
        sizeof(move_seq));
    for(move m = 0; m < TOTAL_BOARD_SIZ; ++m)
        copy_moves(&neighbors_3x3[m], &neighbors_diag[m]);

    memset(out_neighbors4, 0, TOTAL_BOARD_SIZ);
    memset(out_neighbors8, 0, TOTAL_BOARD_SIZ);
    for(u8 i = 0; i < BOARD_SIZ; ++i)
    {
        out_neighbors4[coord_to_move(i, 0)] = out_neighbors4[coord_to_move(0,
            i)] = out_neighbors4[coord_to_move(BOARD_SIZ - 1, i)] =
            out_neighbors4[coord_to_move(i, BOARD_SIZ - 1)] = 1;
        out_neighbors8[coord_to_move(i, 0)] = out_neighbors8[coord_to_move(0,
            i)] = out_neighbors8[coord_to_move(BOARD_SIZ - 1, i)] =
            out_neighbors8[coord_to_move(i, BOARD_SIZ - 1)] = 3;
    }
    out_neighbors4[coord_to_move(0, 0)] = out_neighbors4[coord_to_move(BOARD_SIZ
        - 1, 0)] = out_neighbors4[coord_to_move(0, BOARD_SIZ - 1)] =
        out_neighbors4[coord_to_move(BOARD_SIZ - 1, BOARD_SIZ - 1)] = 2;
    out_neighbors8[coord_to_move(0, 0)] = out_neighbors8[coord_to_move(BOARD_SIZ
        - 1, 0)] = out_neighbors8[coord_to_move(0, BOARD_SIZ - 1)] =
        out_neighbors8[coord_to_move(BOARD_SIZ - 1, BOARD_SIZ - 1)] = 5;

    for(u16 i = 0; i < 256; ++i)
        active_bits_in_byte[i] = count_bits(i);

    for(u8 i = 0; i < BOARD_SIZ; ++i)
        for(u8 j = 0; j < BOARD_SIZ; ++j)
            distances_to_border[coord_to_move(i, j)] = DISTANCE_TO_BORDER(i, j);

    init_moves_by_distance(nei_dst_3, 3, false);
    init_moves_by_distance(nei_dst_4, 4, false);

    init_eye_table();

    flog_info("cons", "board constants calculated");
}
