/*
Support for random play selection while avoiding playing inside a possible eye
and some other fast checks.
*/

#ifndef MATILDA_RANDOM_PLAY_H
#define MATILDA_RANDOM_PLAY_H

#include "config.h"

#include "board.h"
#include "cfg_board.h"
#include "move.h"
#include "types.h"


/*
Select random legal play.
*/
move random_play(
    cfg_board * cb,
    bool is_black
);

/*
Select random legal play.
*/
move random_play2(
    board * b,
    bool is_black
);


#endif
