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
Discover and read opening book files.
*/
void discover_opening_books();

/*
Match an opening rule and return it encoded in the board.
RETURNS true if rule found
*/
bool opening_book(
    out_board * out_b,
    board * state
);


#endif
