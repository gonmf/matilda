/*
For creating and updating Zobrist hashes on board states, both for full board
hashes and position invariant 3x3 hashes.
*/

#include "matilda.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "board.h"
#include "engine.h"
#include "file_io.h"
#include "flog.h"
#include "randg.h"
#include "types.h"

static bool _zobrist_inited = false;

static u64 iv[TOTAL_BOARD_SIZ][2];

/* for 3x3 neighborhood Zobrist hashing */
u16 iv_3x3[TOTAL_BOARD_SIZ][TOTAL_BOARD_SIZ][3];
u16 initial_3x3_hash[TOTAL_BOARD_SIZ];

static u16 get_border_hash_slow(
    move m
){
    u16 ret = 0;
    d8 x;
    d8 y;
    move_to_coord(m, (u8 *)&x, (u8 *)&y);
    u8 shift = 14;
    for(d8 i = x - 1; i <= x + 1; ++i)
        for(d8 j = y - 1; j <= y + 1; ++j)
        {
            if(i == x && j == y)
                continue;
            if(i < 0 || i >= BOARD_SIZ || j < 0 || j >= BOARD_SIZ)
              ret |= (ILLEGAL << shift);

            shift -= 2;
        }
    return ret;
}

/*
Initiate the internal Zobrist table from an external file.
*/
void zobrist_init()
{
    if(_zobrist_inited)
        return;

    alloc_init();

    assert(EMPTY == 0);
    assert(BLACK_STONE < 3);
    assert(WHITE_STONE < 3);
    rand_init();

    char * filename = alloc();
    snprintf(filename, MAX_PAGE_SIZ, "%s%ux%u.zt", data_folder(), BOARD_SIZ,
        BOARD_SIZ);
    if(read_binary_file(iv, sizeof(u64) * TOTAL_BOARD_SIZ * 2, filename) == -1)
    {
        char * s = alloc();
        snprintf(s, MAX_PAGE_SIZ, "could not read %s", filename);
        flog_crit("zbst", s);
        release(s);
    }

    for(move pos = 0; pos < TOTAL_BOARD_SIZ; ++pos)
    {
        u16 shift = 14;
        u8 i;
        u8 j;
        move_to_coord(pos, &i, &j);
        for(d8 x = i - 1; x <= i + 1; ++x)
            for(d8 y = j - 1; y <= j + 1; ++y)
            {
                if(x == i && y == j)
                    continue;
                if(x < 0 || y < 0 || x >= BOARD_SIZ || y >= BOARD_SIZ)
                {
                    shift -= 2;
                    continue;
                }
                move m = coord_to_move(x, y);
                iv_3x3[pos][m][BLACK_STONE - 1] = (BLACK_STONE << shift);
                iv_3x3[pos][m][WHITE_STONE - 1] = (WHITE_STONE << shift);
                iv_3x3[pos][m][ILLEGAL - 1] = (ILLEGAL << shift);
                shift -= 2;
            }
    }

    for(move m = 0; m < TOTAL_BOARD_SIZ; ++m)
        initial_3x3_hash[m] = get_border_hash_slow(m);

    _zobrist_inited = true;

    char * s = alloc();
    snprintf(s, MAX_PAGE_SIZ, "read %s", filename);
    flog_info("zbst", s);
    release(s);
    release(filename);
}

/*
Generate the Zobrist hash of a board state from scratch.
RETURNS Zobrist hash
*/
u64 zobrist_new_hash(
    const board * src
){
    u64 ret = 0;
    for(move m = 0; m < TOTAL_BOARD_SIZ; ++m)
        if(src->p[m] != EMPTY)
            ret ^= iv[m][src->p[m] - 1];
    return ret;
}

/*
Update a Zobrist hash with the piece codification that was there before or will
be after. For Go this means e.g. we pass the codification of a black stone,
regardless of whether we are raplacing an empty point with the stone or
replacing the stone with the point; empty points are not codified.
*/
void zobrist_update_hash(
    u64 * old_hash,
    move m,
    u8 change
){
    *old_hash ^= iv[m][change - 1];
}
