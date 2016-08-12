/*
In Matilda the meaning of dragon is borrowed from GNU Go: a formation of worms
-- groups of stones of the same color connected by adjency -- that *probably*
share the same fate. Their fate may differ via forcing moves and decisions
around kosumi.

Borrowed eyes are eyes of a neighbor group that cannot be shared trasitively.
These are only used internally, the output of an eye count is in the eye field.

These functions still don't deal with some special cases, like the two-headed
dragon:
OOOO....
OXXOOOO.
OX.XXXOO
OXXOOXXO
OXOO.OXO
OXO.OXXO
OXXOOXXO
OOXXX.XO
.OOOOXXO
....OOOO
*/

#ifndef MATILDA_DRAGON_H
#define MATILDA_DRAGON_H

#include "matilda.h"

#include "cfg_board.h"
#include "types.h"

/*
Produce counts of eyes for every group in the board, plus updates the viability
of playing at each position and whether such plays are nakade, from the
perspective of the current player.
*/
void estimate_eyes(
    cfg_board * cb,
    bool is_black,
    bool viable[TOTAL_BOARD_SIZ],
    u8 in_nakade[TOTAL_BOARD_SIZ]
);

#endif
