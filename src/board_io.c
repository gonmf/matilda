/*
More board functions related to cleaning and outputing board states.
*/

#include "matilda.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "board.h"
#include "pts_file.h"
#include "stringm.h"
#include "state_changes.h"
#include "alloc.h"


/*
Clears the contents of a board.
*/
void clear_board(
    board * b
){
    memset(b->p, EMPTY, BOARD_SIZ * BOARD_SIZ);
    b->last_played = b->last_eaten = NONE;
}


/*
Clears the contents of an output board.
*/
void clear_out_board(
    out_board * b
){
    memset(b->tested, false, BOARD_SIZ * BOARD_SIZ);
    b->pass = 0.0;
}


/*
Format a string with a representation of the contents of an output board.
RETURNS string representation
*/
void out_board_to_string(
    char * dst,
    const out_board * src
){
    u16 idx = 0;
    for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
    {
        if(src->tested[m])
            idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, " %4.2f",
                src->value[m]);
        else
            idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "  -- ");

        if(((m + 1) % BOARD_SIZ) == 0)
            idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "\n");
    }
    snprintf(dst + idx, MAX_PAGE_SIZ - idx, "Pass: %4.2f\n", src->pass);
}


/*
Prints the string representation on an output board.
*/
void fprint_out_board(
    FILE * fp,
    const out_board * b
){
    char * s = alloc();
    out_board_to_string(s, b);
    fprintf(fp, "%s", s);
    release(s);
}


/*
Format a string with a representation of the contents of a board, complete with
ko violation indication and subject to the display options of european/japanese
styles (defined in board.h).
RETURNS string representation
*/
void board_to_string(
    char * dst,
    const u8 p[BOARD_SIZ * BOARD_SIZ],
    move last_played,
    move last_eaten
){
    move ko_pos = NONE;
    if(last_eaten != NONE)
    {
        board tmp;
        memcpy(tmp.p, p, BOARD_SIZ * BOARD_SIZ);
        tmp.last_played = last_played;
        tmp.last_eaten = last_eaten;
        u8 own = BLACK_STONE;
        if(is_board_move(last_played) && p[last_played] == BLACK_STONE)
            own = WHITE_STONE;
        if(test_ko(&tmp, last_eaten, own))
            ko_pos = last_eaten;
    }

    u16 idx = 0;

    /*
    Column line
    */
#if (!EUROPEAN_NOTATION) && (BOARD_SIZ > 9)
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "   ");
    for(u8 i = 0; i < BOARD_SIZ; ++i)
        if(i >= 9)
            idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "%2u", (i + 1) / 10);
        else
            idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "  ");
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "\n");
#endif

    if(BOARD_SIZ < 10)
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "  ");
    else
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "   ");

    for(u8 i = 0; i < BOARD_SIZ; ++i)
    {
#if EUROPEAN_NOTATION
        char c = i + 'A';
        if(c >= 'I')
            ++c;
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, " %c", c);
#else
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, " %u", (i + 1) % 10);
#endif
    }

    /*
    Body
    */
    for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
    {
        if((m % BOARD_SIZ == 0))
        {
            move n  = BOARD_SIZ - (m / BOARD_SIZ);
            if(BOARD_SIZ < 10)
                idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "\n%2u", n);
            else
                idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "\n%3u", n);
        }

        u8 x;
        u8 y;
        move_to_coord(m, &x, &y);

        char last_play_indicator = (m == last_played) ? '(' : ((m == last_played
            + 1 && x > 0) ? ')' : ' ');

        switch(p[m])
        {
            case EMPTY:
                if(m == ko_pos)
                {
                    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "%c!",
                        last_play_indicator);
                    break;
                }
                if(is_hoshi_point(m))
                    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "%c+",
                        last_play_indicator);
                else
                    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "%c%c",
                        last_play_indicator, EMPTY_STONE_CHAR);
                break;
            case BLACK_STONE:
                idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "%c%c",
                    last_play_indicator, BLACK_STONE_CHAR);
                break;
            case WHITE_STONE:
                idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "%c%c",
                    last_play_indicator, WHITE_STONE_CHAR);
                break;
            default:
                idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "%c?",
                    last_play_indicator);
        }

        if(x == BOARD_SIZ - 1)
        {
            last_play_indicator = (m == last_played) ? ')' : ' ';
            move n  = BOARD_SIZ - (m / BOARD_SIZ);
            if(BOARD_SIZ < 10)
                idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "%c%u",
                    last_play_indicator, n);
            else
                idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "%c%2u",
                    last_play_indicator, n);
        }

    }

    /*
    Column line
    */
    if(BOARD_SIZ < 10)
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "\n  ");
    else
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "\n   ");

    for(u8 i = 0; i < BOARD_SIZ; ++i)
    {
#if EUROPEAN_NOTATION
        char c = i + 'A';
        if(c >= 'I')
            ++c;
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, " %c", c);
#else
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, " %u", i >= 9 ? (i + 1) /
            10 : (i + 1) % 10);
#endif
    }
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "\n");

#if (!EUROPEAN_NOTATION) && (BOARD_SIZ > 9)
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "   ");
    for(u8 i = 0; i < BOARD_SIZ; ++i)
        if(i >= 9)
            idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "%2u", (i + 1) % 10);
        else
            idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "  ");
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "\n");
#endif

    if(last_played == PASS)
        snprintf(dst + idx, MAX_PAGE_SIZ - idx, "\nLast play was a pass\n");
    else
        if(last_played != NONE)
        {
            char * mstr = alloc();
#if EUROPEAN_NOTATION
            coord_to_alpha_num(mstr, last_played);
#else
            coord_to_num_num(mstr, last_played);
#endif
            snprintf(dst + idx, MAX_PAGE_SIZ - idx, "\nLast played %s\n", mstr);
            release(mstr);
        }
}


/*
Print a board string representation.
*/
void fprint_board(
    FILE * fp,
    const board * b
){
    char * s = alloc();
    board_to_string(s, b->p, b->last_played, b->last_eaten);
    fprintf(fp, "%s", s);
    release(s);
}


