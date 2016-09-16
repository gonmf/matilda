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

#include "matilda.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "alloc.h"
#include "board.h"
#include "flog.h"
#include "matrix.h"
#include "move.h"
#include "stringm.h"
#include "types.h"


/*
RETURNS true if move is a stone play
*/
bool is_board_move(
    move m
){
    return m < TOTAL_BOARD_SIZ;
}

/*
Converts a (x,y) representation into a move.
Warning: Ignores passes
RETURNS move
*/
move coord_to_move(
    u8 x,
    u8 y
){
    assert(x < BOARD_SIZ && y < BOARD_SIZ);
    return (move)(y * BOARD_SIZ + x);
}

/*
Converts a move representation into a (x,y) one.
*/
void move_to_coord(
    move m,
    u8 * restrict x,
    u8 * restrict y
){
    assert(is_board_move(m));
    *x = (u8)(m % BOARD_SIZ);
    *y = (u8)(m / BOARD_SIZ);
}

/*
RETURNS the Manhattan distance between two points
*/
u8 coord_distance(
    u8 p1[2],
    u8 p2[2]
){
    assert(p1[0] < BOARD_SIZ);
    assert(p2[0] < BOARD_SIZ);

    u8 ret;
    if(p1[0] > p2[0])
        ret = p1[0] - p2[0];
    else
        ret = p2[0] - p1[0];

    if(p1[1] > p2[1])
        ret += p1[1] - p2[1];
    else
        ret += p2[1] - p1[1];

    return ret;
}


/*
RETURNS the Manhattan distance between two points
*/
u8 move_distance(
    move a,
    move b
){
    assert(is_board_move(a));
    assert(is_board_move(b));

    u8 p1[2];
    u8 p2[2];
    move_to_coord(a, &p1[0], &p1[1]);
    move_to_coord(b, &p2[0], &p2[1]);

    return coord_distance(p1, p2);
}

/*
Produces the move correspondent in the transformed matrix.
RETURNS move
*/
move reduce_move(
    move m,
    d8 method
){
    if(!is_board_move(m))
        return m;

    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);

    reduce_coord(&x, &y, BOARD_SIZ, method);

    return coord_to_move(x, y);
}

/*
Parses a string for a move value, in the format D4.
The value I is skipped.
RETURNS move
*/
move coord_parse_alpha_num(
    const char * s
){
    u32 len = strlen(s);
    if(len != 2 && len != 3)
        return NONE;

    char c1 = low_char(s[0]);
    u8 i1 = c1 > 'i' ? c1 - 'b' : c1 - 'a';
    d32 i2;
    if(!parse_int(&i2, s + 1))
        return NONE;

    i2 = BOARD_SIZ - i2;

    if(i1 >= BOARD_SIZ || i2 >= BOARD_SIZ || i2 < 0)
        return NONE;

    return coord_to_move(i1, i2);
}

/*
Parses a string for a move value, in the format DE.
The character I is allowed.
RETURNS move
*/
move coord_parse_alpha_alpha(
    const char * s
){
    u32 len = strlen(s);
    if(len != 2)
        return NONE;

    char c1 = low_char(s[0]);
    char c2 = low_char(s[1]);

    u8 i1 = c1 - 'a';
    u8 i2 = c2 - 'a';

    if(i1 >= BOARD_SIZ || i2 >= BOARD_SIZ)
        return NONE;

    return coord_to_move(i1, i2);
}

/*
Parses a string for a move value, in the format 4-4.
RETURNS move
*/
move coord_parse_num_num(
    const char * s
){
    u32 len = strlen(s);
    if(len < 3 || len > 5)
        return NONE;

    char buf[8];
    strncpy(buf, s, 8);


    char * c1 = strtok(buf, "-");
    if(c1 == NULL)
        return NONE;
    d32 i1;
    if(!parse_int(&i1, c1))
        return NONE;
    if(i1 < 1 || i1 > BOARD_SIZ)
        return NONE;

    char * c2 = strtok(NULL, "-");
    if(c2 == NULL)
        return NONE;
    d32 i2;
    if(!parse_int(&i2, c2))
        return NONE;
    if(i2 < 1 || i2 > BOARD_SIZ)
        return NONE;

    return coord_to_move(i1 - 1, i2 - 1);
}

/*
Converts a move to a string representation, like 4-4.
*/
void coord_to_num_num(
    char * dst,
    move m
){
    assert(is_board_move(m));
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);

    snprintf(dst, 8, "%u-%u", x + 1, y + 1);
}

/*
Converts a move to a string representation, like D4.
The value I is skipped.
*/
void coord_to_alpha_num(
    char * dst,
    move m
){
    assert(is_board_move(m));
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);
    char c = x + 'A';
    if(c >= 'I')
        ++c;

    snprintf(dst, 8, "%c%u", c,  BOARD_SIZ - y);
}

/*
Converts a move to a string representation, like DD.
The character I is allowed.
*/
void coord_to_alpha_alpha(
    char * dst,
    move m
){
    assert(is_board_move(m));
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);

    snprintf(dst, 8, "%c%c", x + 'a', y + 'a');
}

/*
Populates a move_seq structure with the moves of distance equal or closer to
distance, for every intersection of a board.
*/
void init_moves_by_distance(
    move_seq neighbours[TOTAL_BOARD_SIZ],
    u16 distance,
    bool include_own
){
    for(move a = 0; a < TOTAL_BOARD_SIZ; ++a)
    {
        move c = 0;
        for(move b = 0; b < TOTAL_BOARD_SIZ; ++b)
            if(include_own || a != b)
                if(move_distance(a, b) <= distance)
                {
                    neighbours[a].coord[c] = b;
                    ++c;
                }
        neighbours[a].count = c;
    }
}

/*
Copies the information from one move_seq structure, appending it at the tail of
another
*/
void copy_moves(
    move_seq * restrict dst,
    const move_seq * restrict src
){
    assert(dst->count + src->count < TOTAL_BOARD_SIZ);
    memcpy(dst->coord + dst->count, src->coord, src->count * sizeof(move));
    dst->count += src->count;
}

/*
Add move to a move sequence structure.
Does not test if move is not already present.
*/
void add_move(
    move_seq * ms,
    move m
){
    ms->coord[ms->count] = m;
    ms->count++;
}

/*
Remove the given move from the move sequence structure.
Crashes if the move is not found.
*/
void rem_move(
    move_seq * ms,
    move m
){
    for(u16 i = 0; i < ms->count; ++i)
        if(ms->coord[i] == m)
        {
            ms->count--;
            ms->coord[i] = ms->coord[ms->count];
            return;
        }

    flog_crit("move_seq", "move not found\n");
}

