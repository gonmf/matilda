/*
Support for random play selection while avoiding playing inside a possible eye
and some other fast checks.
*/

#ifndef MATILDA_RANDOM_PLAY_H
#define MATILDA_RANDOM_PLAY_H

#include "matilda.h"

#include "board.h"
#include "cfg_board.h"
#include "types.h"


/*
Select random safe play.
*/
move select_safe_play_random(
    cfg_board * cb,
    bool is_black
);

/*
Randomly select a safe play.
*/
void random_play(
    const board * b,
    bool is_black,
    out_board * out_b
);


#endif
