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
#include "buffer.h"


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
const char * out_board_to_string(
    const out_board * b
){
    char * buf = get_buffer();

    u16 idx = 0;
    for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
    {
        if(b->tested[m])
            idx += snprintf(buf + idx, 2048 - idx, " %4.2f",
                b->value[m]);
        else
            idx += snprintf(buf + idx, 2048 - idx, "  -- ");

        if(((m + 1) % BOARD_SIZ) == 0)
            idx += snprintf(buf + idx, 2048 - idx, "\n");
    }
    idx += snprintf(buf + idx, 2048 - idx, "Pass: %4.2f\n",
        b->pass);
    return buf;
}


/*
Prints the string representation on an output board.
*/
void fprint_out_board(
    FILE * fp,
    const out_board * b
){
    fprintf(fp, "%s", out_board_to_string(b));
}


/*
Format a string with a representation of the contents of a board, complete with
ko violation indication and subject to the display options of european/japanese
styles (defined in board.h).
RETURNS string representation
*/
const char * board_to_string(
    const u8 p[BOARD_SIZ * BOARD_SIZ],
    move last_played,
    move last_eaten
){
    char * buf = get_buffer();

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
    idx += snprintf(buf + idx, 2048 - idx, "   ");
    for(u8 i = 0; i < BOARD_SIZ; ++i)
        if(i >= 9)
            idx += snprintf(buf + idx, 2048 - idx, "%2u", (i + 1) / 10);
        else
            idx += snprintf(buf + idx, 2048 - idx, "  ");
    idx += snprintf(buf + idx, 2048 - idx, "\n");
#endif

    if(BOARD_SIZ < 10)
        idx += snprintf(buf + idx, 2048 - idx, "  ");
    else
        idx += snprintf(buf + idx, 2048 - idx, "   ");

    for(u8 i = 0; i < BOARD_SIZ; ++i)
    {
#if EUROPEAN_NOTATION
        char c = i + 'A';
        if(c >= 'I')
            ++c;
        idx += snprintf(buf + idx, 2048 - idx, " %c", c);
#else
        idx += snprintf(buf + idx, 2048 - idx, " %u", (i + 1) % 10);
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
                idx += snprintf(buf + idx, 2048 - idx, "\n%2u", n);
            else
                idx += snprintf(buf + idx, 2048 - idx, "\n%3u", n);
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
                    idx += snprintf(buf + idx, 2048 - idx, "%c!",
                        last_play_indicator);
                    break;
                }
                if(is_hoshi_point(m))
                    idx += snprintf(buf + idx, 2048 - idx, "%c+",
                        last_play_indicator);
                else
                    idx += snprintf(buf + idx, 2048 - idx, "%c%c",
                        last_play_indicator, EMPTY_STONE_CHAR);
                break;
            case BLACK_STONE:
                idx += snprintf(buf + idx, 2048 - idx, "%c%c",
                    last_play_indicator, BLACK_STONE_CHAR);
                break;
            case WHITE_STONE:
                idx += snprintf(buf + idx, 2048 - idx, "%c%c",
                    last_play_indicator, WHITE_STONE_CHAR);
                break;
            default:
                idx += snprintf(buf + idx, 2048 - idx, "%c?",
                    last_play_indicator);
        }

        if(x == BOARD_SIZ - 1)
        {
            last_play_indicator = (m == last_played) ? ')' : ' ';
            move n  = BOARD_SIZ - (m / BOARD_SIZ);
            if(BOARD_SIZ < 10)
                idx += snprintf(buf + idx, 2048 - idx, "%c%u",
                    last_play_indicator, n);
            else
                idx += snprintf(buf + idx, 2048 - idx, "%c%2u",
                    last_play_indicator, n);
        }

    }

    /*
    Column line
    */
    if(BOARD_SIZ < 10)
        idx += snprintf(buf + idx, 2048 - idx, "\n  ");
    else
        idx += snprintf(buf + idx, 2048 - idx, "\n   ");

    for(u8 i = 0; i < BOARD_SIZ; ++i)
    {
#if EUROPEAN_NOTATION
        char c = i + 'A';
        if(c >= 'I')
            ++c;
        idx += snprintf(buf + idx, 2048 - idx, " %c", c);
#else
        idx += snprintf(buf + idx, 2048 - idx, " %u", i >= 9 ? (i + 1) / 10 : (i
            + 1) % 10);
#endif
    }
    idx += snprintf(buf + idx, 2048 - idx, "\n");

#if (!EUROPEAN_NOTATION) && (BOARD_SIZ > 9)
    idx += snprintf(buf + idx, 2048 - idx, "   ");
    for(u8 i = 0; i < BOARD_SIZ; ++i)
        if(i >= 9)
            idx += snprintf(buf + idx, 2048 - idx, "%2u", (i + 1) % 10);
        else
            idx += snprintf(buf + idx, 2048 - idx, "  ");
    idx += snprintf(buf + idx, 2048 - idx, "\n");
#endif

    if(last_played == PASS)
        idx += snprintf(buf + idx, 2048 - idx,
            "\nLast play was a pass\n");
    else
        if(last_played != NONE)
        {
#if EUROPEAN_NOTATION
            idx += snprintf(buf + idx, 2048 - idx,
                "\nLast played %s\n", coord_to_alpha_num(last_played));
#else
            idx += snprintf(buf + idx, 2048 - idx,
                "\nLast played %s\n", coord_to_num_num(last_played));
#endif
        }

    return buf;
}


/*
Print a board string representation.
*/
void fprint_board(
    FILE * fp,
    const board * b
){
    fprintf(fp, "%s", board_to_string(b->p, b->last_played, b->last_eaten));
}


