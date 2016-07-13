/*
Support for random play selection while avoiding playing inside a possible eye
and some other fast checks.
*/

#include "matilda.h"

#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "cfg_board.h"
#include "randg.h"
#include "state_changes.h"
#include "tactical.h"


/*
Select random safe play.
*/
move select_safe_play_random(
    cfg_board * cb,
    bool is_black
){
    u8 libs;
    bool captures;
    u16 attempts = cb->empty.count;
    u8 opt = is_black ? WHITE_STONE : BLACK_STONE;

    u16 empty_intersections = cb->empty.count;

    bool in_seki[BOARD_SIZ * BOARD_SIZ];
    mark_pts_in_seki(cb, in_seki);

    do
    {
        u16 p = rand_u16(empty_intersections);
        move m = cb->empty.coord[p];

        if(!is_eye(cb, m, is_black) && !ko_violation(cb, m) && (libs =
            safe_to_play(cb, m, is_black, &captures)) > 0){

            /*
            Don't play equal point sekis
            */
            if(in_seki[m])
                continue;

            /*
            Don't follow obvious ladders
            */
            if(libs == 2 && is_ladder(cb, m, is_black))
                continue;

            /*
            Prohibit self-ataris except throw-ins and filling dead groups
            */
            if(libs == 1 && captures == 0 && !puts_neighbor_in_atari(cb, m, opt))
                continue;

            return m;
        }
    }
    while(--attempts);

    move playable[BOARD_SIZ * BOARD_SIZ];
    u16 playable_count = 0;

    for(u16 k = 0; k < cb->empty.count; ++k)
    {
        move m = cb->empty.coord[k];

        if(!is_eye(cb, m, is_black) && !ko_violation(cb, m) && (libs =
            safe_to_play(cb, m, is_black, &captures)) > 0)
        {

            /*
            Don't play equal point sekis
            */
            if(in_seki[m])
                continue;

            /*
            Don't follow obvious ladders
            */
            if(libs == 2 && is_ladder(cb, m, is_black))
                continue;

            /*
            Prohibit self-ataris except throw-ins and filling dead groups
            */
            if(libs == 1 && captures == 0 && !puts_neighbor_in_atari(cb, m,
                opt))
                continue;

            playable[playable_count] = m;
            ++playable_count;
        }
    }

    if(playable_count > 0)
    {
        u16 p = rand_u16(playable_count);
        return playable[p];
    }

    return PASS;
}


/*
Randomly select a safe play.
*/
void random_play(
    const board * b,
    bool is_black,
    out_board * out_b
){
    rand_init();

    clear_out_board(out_b);
    cfg_board cb;
    cfg_from_board(&cb, b);
    move m = select_safe_play_random(&cb, is_black);
    cfg_board_free(&cb);
    if(m == PASS)
        out_b->pass = 1.0;
    else
    {
        out_b->tested[m] = 1.0;
        out_b->value[m] = 1.0;
    }
}

