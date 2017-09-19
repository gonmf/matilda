/*
For creating and updating Zobrist hashes on board states, both for full board
hashes and position invariant 3x3 hashes.
*/

#ifndef MATILDA_ZOBRIST_H
#define MATILDA_ZOBRIST_H

#include "config.h"

#include "types.h"

/*
Initiate the internal Zobrist table from an external file.
*/
void zobrist_init();

/*
Generate the Zobrist hash of a board state from scratch.
RETURNS Zobrist hash
*/
u64 zobrist_new_hash(
    const board * src
);

/*
Update a Zobrist hash with the piece codification that was there before or will
be after. For Go this means e.g. we pass the codification of a black stone,
regardless of whether we are raplacing an empty point with the stone or
replacing the stone with the point; empty points are not codified.
*/
void zobrist_update_hash(
    u64 * old_hash,
    move m,
    u8 change
);

#endif
