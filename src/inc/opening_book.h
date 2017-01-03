/*
Strategy that makes use of an opening book.
*/

#ifndef MATILDA_OPENING_BOOK_H
#define MATILDA_OPENING_BOOK_H

#include "matilda.h"

#include "types.h"
#include "board.h"
#include "move.h"

typedef struct __ob_entry_ {
    u32 hash;
    u8 p[PACKED_BOARD_SIZ];
    move play;
    struct __ob_entry_ * next;
} ob_entry;



/*
Formats a board position to a Fuego-style opening book rule, for example:
13 K4 C3 | F11
With no line break. Does not ascertain the validity of the rule, i.e. do not
invoke after a capture or pass has occurred.
*/
void board_to_ob_rule(
    char * dst,
    u8 p[TOTAL_BOARD_SIZ],
    move play
);

/*
Discover and read opening book files.
*/
void opening_book_init();

/*
Match an opening rule and return it encoded in the board.
RETURNS true if rule found
*/
bool opening_book(
    out_board * out_b,
    board * state
);

#endif
