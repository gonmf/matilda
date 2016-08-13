/*
A cfg_board structure is a common fate graph board representation that is used
for fast atari checking; because of this it is useful specially in heavy
playouts.

A cfg_board is built from a previous board structure, but the two are not
linked; i.e. changes in one don't reflect in the other.

Building and destroying (freeing) a cfg_board are costly operations that should
be used only if the cfg_board will be used in playing many turns. cfg_board
structures are partially dynamically created and as such cannot be simply
memcpied to reuse the same starting game point. Undo is also not supported.

Freed cfg_board information is kept in cache for fast access in the future; it
is best to first free previous instances before creating new ones, thus limiting
the size the cache has to have.

Just like in the rest of the source code, all functions are not thread unless
explicitly said so.
*/

#ifndef MATILDA_CFG_BOARD_H
#define MATILDA_CFG_BOARD_H

#include "matilda.h"

#include <unistd.h>
#include <stdio.h>

#include "board.h"
#include "move.h"
#include "types.h"

#define LIB_BITMAP_SIZ (TOTAL_BOARD_SIZ / 8 + 1)

#define MAX_GROUPS (((BOARD_SIZ / 2) + 1) * BOARD_SIZ)

#define MAX_NEIGHBORS \
    (((BOARD_SIZ / 2) + 1) * (BOARD_SIZ / 2) + (BOARD_SIZ / 2) + 1)

typedef struct __group_ {
	bool is_black;
	u8 unique_groups_idx;
	u8 liberties;
	u8 ls[LIB_BITMAP_SIZ];
	move liberties_min_coord;
	move_seq stones; /* stone 0 if used as representative */
	u8 neighbors_count;
	move neighbors[MAX_NEIGHBORS]; /* move id of neighbors */
	u8 eyes;
	u8 borrowed_eyes;
	struct __group_ * next;
} group;

/*
unique_groups stores IDs of groups, which are the value of a stone that belongs
to that group, and the g field specifies the group that possesses a certain
intersection (or NULL). So to get the group do cb->g[unique_groups[idx]].
*/
typedef struct __cfg_board_ {
	u8 p[TOTAL_BOARD_SIZ];
	move last_eaten;
	move last_played;
	u16 hash[TOTAL_BOARD_SIZ]; /* hash of the 3x3 neighborhoods */
	move_seq empty; /* free positions of the board */
	u8 black_neighbors4[TOTAL_BOARD_SIZ]; /* stones in the neighborhood */
	u8 white_neighbors4[TOTAL_BOARD_SIZ];
	u8 black_neighbors8[TOTAL_BOARD_SIZ];
	u8 white_neighbors8[TOTAL_BOARD_SIZ];
	u8 unique_groups_count;
	move unique_groups[MAX_GROUPS];
	group * g[TOTAL_BOARD_SIZ]; /* CFG stone groups or NULL if empty */
} cfg_board;



/*
Initialize the CFG board support. Must be called before most other functions.
*/
void cfg_board_init();



/*
Tests if the two structures have the same board contents.
RETURNS true if the structures are equal in board contents
*/
bool cfg_board_are_equal(
    cfg_board * restrict a,
    const board * restrict b
);

/*
Initiliazes the data pointed to cb, to hold a valid (but empty) board.
*/
void cfg_init_board(
    cfg_board * cb
);

/*
Converts a board structure into an cfg_board structure; the two are not linked;
changing one will not modify the other.
*/
void cfg_from_board(
    cfg_board * restrict dst,
    const board * restrict src
);

/*
Clones a CFG board into another, independent, instance.
*/
void cfg_board_clone(
    cfg_board * restrict dst,
    const cfg_board * restrict src
);

/*
Apply a passing turn.
*/
void just_pass(
    cfg_board * cb
);

/*
Assume play is legal and update the structure, capturing
accordingly.
*/
void just_play(
    cfg_board * cb,
    move m,
    bool is_black
);

/*
Assume play is legal and update the structure, capturing accordingly.
Also updates a stone difference.
*/
void just_play1(
    cfg_board * cb,
    move m,
    bool is_black,
    d16 * stone_difference
);


/*
Assume play is legal and update the structure, capturing
accordingly.
Also updates a Zobrist hash value.
*/
void just_play2(
    cfg_board * cb,
    move m,
    bool is_black,
    u64 * zobrist_hash
);

/*
Assume play is legal and update the structure, capturing accordingly. Also
updates a stone difference and fills a matrix of captured stones and a bitmap of
liberties of neighbors of the captured groups. Does NOT clear the matrix and
bitmap.
*/
void just_play3(
    cfg_board * cb,
    move m,
    bool is_black,
    d16 * stone_difference,
    bool stones_removed[TOTAL_BOARD_SIZ],
    u8 rem_nei_libs[LIB_BITMAP_SIZ]
);


/*
Detects one stone ko rule violations.
Doesn't test other types of legality.
RETURNS true if play is illegal due to ko
*/
bool ko_violation(
    const cfg_board * cb,
    move m
);

/*
If ko is possible, returns the offending play.
RETURNS position in ko, or NONE
*/
move ko_get_play(
    const cfg_board * cb
);

/*
Calculates the liberties after playing and the number of stones captured.
RETURNS number of liberties after play
*/
u8 libs_after_play(
    cfg_board * cb,
    move m,
    bool is_black,
    move * caps
);

/*
Calculates if playing at the designated position is legal and safe.
Also returns whether it would return in a capture.
RETURNS 0 for illegal, 1 for placed in atari, 2 for safe to play
*/
u8 safe_to_play(
    cfg_board * cb,
    move m,
    bool is_black,
    bool * caps
);

/*
Tests if a play captures any opponent stone.
RETURNS true if any opponent stone is captured
*/
bool caps_after_play(
    const cfg_board * cb,
    move m, bool is_black
);

/*
RETURNS true if play is valid (validating ko rule)
*/
bool can_play(
    const cfg_board * cb,
    move m,
    bool is_black
);

/*
RETURNS true if play is valid (ignoring ko rule)
*/
bool can_play_ignoring_ko(
    const cfg_board * cb,
    move m,
    bool is_black
);

/*
Returns the first liberty found of the group (in no particular order).
RETURNS a liberty of the group
*/
move get_1st_liberty(
    const group * g
);


/*
Returns a liberty of the group after the specified point.
If the group has no more liberties then NONE is returned instead.
RETURNS a liberty of the group
*/
move get_next_liberty(
    const group * g,
    move start /* exclusive */
);



/*
Get closest group in the 3x3 neighborhood of a point.
RETURNS group pointer or NULL
*/
group * get_closest_group(
    const cfg_board * cb,
    move m
);


/*
Frees the structure information dynamically allocated (not the actual cfg_board
structure).
*/
void cfg_board_free(
    cfg_board * cb
);

/*
Print structure information for debugging.
*/
void fprint_cfg_board(
    FILE * fp,
    const cfg_board * cb
);


/*
Verify the integrity of a CFG board structure.
*/
bool verify_cfg_board(
    const cfg_board * cb
);

#endif
