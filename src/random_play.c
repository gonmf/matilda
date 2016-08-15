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
#include "tactical.h"


/*
Select random legal play.
*/
move random_play(
    cfg_board * cb,
    bool is_black
){
    bool _ignored;

    move playable[TOTAL_BOARD_SIZ];
    u16 playable_count = 0;

    for(u16 k = 0; k < cb->empty.count; ++k)
    {
        move m = cb->empty.coord[k];

        if(!is_eye(cb, m, is_black) && !ko_violation(cb, m) &&
            safe_to_play(cb, m, is_black, &_ignored) > 0)
        {
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
Select random legal play.
*/
move random_play2(
    board * b,
    bool is_black
){
    cfg_board cb;
    cfg_from_board(&cb, b);
    move ret = random_play(&cb, is_black);
    cfg_board_free(&cb);
    return ret;
}

