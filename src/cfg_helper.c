/*
Collection of functions over a CFG boars structure that are not related to
actual state changes nor tactical evaluation; though they may still be useful.
*/

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "alloc.h"
#include "board.h"
#include "cfg_board.h"
#include "flog.h"
#include "move.h"
#include "types.h"


extern u8 out_neighbors8[TOTAL_BOARD_SIZ];
extern u8 out_neighbors4[TOTAL_BOARD_SIZ];
extern move_seq neighbors_side[TOTAL_BOARD_SIZ];
extern move_seq neighbors_diag[TOTAL_BOARD_SIZ];
extern move_seq neighbors_3x3[TOTAL_BOARD_SIZ];
extern bool border_left[TOTAL_BOARD_SIZ];
extern bool border_right[TOTAL_BOARD_SIZ];
extern bool border_top[TOTAL_BOARD_SIZ];
extern bool border_bottom[TOTAL_BOARD_SIZ];
extern u8 active_bits_in_byte[256];


/*
Returns the first liberty found of the group (in no particular order).
RETURNS a liberty of the group
*/
move get_1st_liberty(
    const group * g
){
    assert(g->liberties > 0);

    for(u8 i = 0; i < LIB_BITMAP_SIZ; ++i)
        if(g->ls[i])
        {
            u8 j;
            for(j = 0; j < 7; ++j)
                if(g->ls[i] & (1 << j))
                    break;
            return i * 8 + j;
        }

    flog_crit("cfg", "CFG group has no liberties");
    exit(EXIT_FAILURE); /* this is unnecessary but mutes erroneous complaints */
}


/*
Returns a liberty of the group after the specified point.
If the group has no more liberties then NONE is returned instead.
RETURNS a liberty of the group
*/
move get_next_liberty(
    const group * g,
    move start /* exclusive */
){
    ++start;
    for(move m = start; m < TOTAL_BOARD_SIZ; ++m)
    {
        u8 mask = (1 << (m % 8));
        if(g->ls[m / 8] & mask)
            return m;
    }

    return NONE;
}


/*
Get closest group in the 3x3 neighborhood of a point.
RETURNS group pointer or NULL
*/
group * get_closest_group(
    const cfg_board * cb,
    move m
){
    for(u8 k = 0; k < neighbors_3x3[m].count; ++k)
    {
        move n = neighbors_3x3[m].coord[k];
        if(cb->g[n] != NULL)
            return cb->g[n];
    }
    return NULL;
}


/*
Return the minimum amount of liberties of groups with stones adjacent to an
intersection.
RETURNS minimum number of liberties found, or NONE
*/
u16 min_neighbor_libs(
    const cfg_board * cb,
    move m,
    u8 stone
){
    assert(is_board_move(m));

    u16 ret = NONE;
    if(!border_left[m] && cb->p[m + LEFT] == stone)
        ret = cb->g[m + LEFT]->liberties;
    if(!border_right[m] && cb->p[m + RIGHT] == stone && cb->g[m +
        RIGHT]->liberties < ret)
        ret = cb->g[m + RIGHT]->liberties;
    if(!border_top[m] && cb->p[m + TOP] == stone && cb->g[m + TOP]->liberties <
        ret)
        ret = cb->g[m + TOP]->liberties;
    if(!border_bottom[m] && cb->p[m + BOTTOM] == stone && cb->g[m +
        BOTTOM]->liberties < ret)
        ret = cb->g[m + BOTTOM]->liberties;
    return ret;
}


/*
Return the maximum amount of liberties of groups with stones adjacent to an
intersection.
RETURNS maximum number of liberties found, or 0
*/
u8 max_neighbor_libs(
    const cfg_board * cb,
    move m,
    u8 stone
){
    assert(is_board_move(m));

    u8 ret = 0;
    if(!border_left[m] && cb->p[m + LEFT] == stone)
        ret = cb->g[m + LEFT]->liberties;
    if(!border_right[m] && cb->p[m + RIGHT] == stone && cb->g[m +
        RIGHT]->liberties > ret)
        ret = cb->g[m + RIGHT]->liberties;
    if(!border_top[m] && cb->p[m + TOP] == stone && cb->g[m + TOP]->liberties >
        ret)
        ret = cb->g[m + TOP]->liberties;
    if(!border_bottom[m] && cb->p[m + BOTTOM] == stone && cb->g[m +
        BOTTOM]->liberties > ret)
        ret = cb->g[m + BOTTOM]->liberties;
    return ret;
}

/*
Tests whether a neighbor group of stone type stone has two liberties.
RETURNS true if neighbor group is put in atari
*/
bool puts_neighbor_in_atari(
    const cfg_board * cb,
    move m,
    u8 stone
){
    if(!border_left[m] && cb->p[m + LEFT] == stone && cb->g[m + LEFT]->liberties
        == 2)
        return true;
    if(!border_right[m] && cb->p[m + RIGHT] == stone && cb->g[m +
        RIGHT]->liberties == 2)
        return true;
    if(!border_top[m] && cb->p[m + TOP] == stone && cb->g[m + TOP]->liberties ==
        2)
        return true;
    return (!border_bottom[m] && cb->p[m + BOTTOM] == stone && cb->g[m +
        BOTTOM]->liberties == 2);
}

/*
Return the maximum number of stones of a group of stones of value stone;
adjacent to the intersection m.
RETURNS maximum number of stones of a group, or 0
*/
u16 max_neighbor_group_stones(
    const cfg_board * cb,
    move m,
    u8 stone
){
    assert(is_board_move(m));

    u16 ret = 0;
    if(!border_left[m] && cb->p[m + LEFT] == stone)
        ret = cb->g[m + LEFT]->stones.count;
    if(!border_right[m] && cb->p[m + RIGHT] == stone && cb->g[m +
        RIGHT]->stones.count > ret)
        ret = cb->g[m + RIGHT]->stones.count;
    if(!border_top[m] && cb->p[m + TOP] == stone && cb->g[m +
        TOP]->stones.count > ret)
        ret = cb->g[m + TOP]->stones.count;
    if(!border_bottom[m] && cb->p[m + BOTTOM] == stone && cb->g[m +
        BOTTOM]->stones.count > ret)
        ret = cb->g[m + BOTTOM]->stones.count;
    return ret;
}

/*
Tests whether two groups have exactly the same liberties.
RETURNS true if the groups have the exact same liberties
*/
bool groups_same_liberties(
    const group * restrict g1,
    const group * restrict g2
){
    return memcmp(g1->ls, g2->ls, LIB_BITMAP_SIZ) == 0;
}

/*
Tests whether two groups share at least one liberty.
RETURNS true if the groups share at least one liberty
*/
bool groups_share_liberties(
    const group * restrict g1,
    const group * restrict g2
){
    for(u8 i = 0; i < LIB_BITMAP_SIZ; ++i)
        if((g1->ls[i] & g2->ls[i]) > 0)
            return true;
    return false;
}

/*
Counts the number of shared liberties between two groups.
RETURNS number of shared liberties
*/
u8 groups_shared_liberties(
    const group * restrict g1,
    const group * restrict g2
){
    u8 ret = 0;
    for(u8 i = 0; i < LIB_BITMAP_SIZ; ++i)
        ret += active_bits_in_byte[g1->ls[i] & g2->ls[i]];
    return ret;
}
