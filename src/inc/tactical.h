/*
Tactical functions that make use of a cfg_board structure.

Tactical analysis in Matilda mostly concerns with two things:
- Eye shape -- eyes, nakade
- Life and death -- ladders, seki, 1-2 liberty solvers for killing and saving
groups, connecting groups by kosumi, bamboo joints, etc for the
purpose of eye counting.
*/

#ifndef MATILDA_TACTICAL_H
#define MATILDA_TACTICAL_H

#include "matilda.h"

#include "cfg_board.h"
#include "move.h"
#include "types.h"


/*
RETURNS true if move coordinate is that of an eye
*/
bool is_eye(
    const cfg_board * cb,
    move m,
    bool is_black
);

/*
A relaxed eye is an eye that may eventually become untakeable (without playing
at the empty intersection itself). Examples:

.bw   .b.   ---   +--
b*b   b*b   b*b   |*b
.bb   .bb   .b.   |b.

RETURNS true if coordinate is that of a relaxed eye
*/
bool is_relaxed_eye(
    const cfg_board * cb,
    move m,
    bool is_black
);

/*
Detects the corner of a 2-point eye. The eye may be attackable, and therefore
used as a forcing move, hence the presence of the can_have_forcing_move
parameter, that is made active if the eye shape may be attacked. It doesn't need
to be set prior to calling. This functions assumes it is called in the left-top
most point, and won't detect the shape otherwise.
RETURNS true if eye space is a 2-point eye
*/
bool is_2pt_eye(
    const cfg_board * cb,
    move m,
    bool is_black,
    bool * can_have_forcing_move
);

/*
Detects a 4-point squared eye. The eye may be attackable, and therefore used as
a forcing move, hence the presence of the can_have_forcing_move parameter, that
is made active if the eye shape may be attacked. It doesn't need to be set prior
to calling. This functions assumes it is called in the left-top most point, and
won't detect the shape otherwise.
RETURNS true if eye space is a 4-point eye
*/
bool is_4pt_eye(
    const cfg_board * cb,
    move m,
    bool is_black,
    bool * can_have_forcing_move
);

/*
A point surrounded by stones of the same color. It is not a safe eye but if
being the connector between groups with at least one independent liberty, it
becomes an eye.
RETURNS true if m is a connecting liberty
*/
bool sheltered_liberty(
    const cfg_board * cb,
    move m
);

/*
Returns true if point is the corner of a well defended shape. Example:
XXX
X*.
XXX
RETURNS true if m is corner liberty
*/
bool is_corner_liberty(
    const cfg_board * cb,
    move m,
    bool is_black
);

/*
Tests whether the point if an empty space inside a bamboo joint:
XX
*.
XX
(* is the point being tested)
RETURNS true if inside a bamboo joint
*/
bool is_vertical_bamboo_joint(
    const cfg_board * cb,
    move m
);

/*
Tests whether the point if an empty space inside a bamboo joint:
X*X
X.X
(* is the point being tested)
RETURNS true if inside a bamboo joint
*/
bool is_horizontal_bamboo_joint(
    const cfg_board * cb,
    move m
);

/*
Tests whether the point is an empty space besides a kosumi (by the same player),
of the type:
*X
X.
(* is the point being tested)
RETURNS true if point is next to a kosumi
*/
bool is_kosumi1(
    const cfg_board * cb,
    move m
);

/*
Tests whether the point is an empty space besides a kosumi (by the same player),
of the type:
X*
.X
(* is the point being tested)
RETURNS true if point is next to a kosumi
*/
bool is_kosumi2(
    const cfg_board * cb,
    move m
);

/*
Tests for nakade, of the types straight three, bent three, pyramid four, crossed
five, bulky five and rabbity six. This doesn't test if the eyes are proper;
might be an incomplete nakade, but probably still a promising play to make:

b b b b b b b
b w w w w 2 b
b w . 1 . w b
b 3 w w w w b
b b b b b b b

(1 comes first, 2 and 3 are miai)
It also doesn't test for the liberties; i.e. if the only liberties are inside
the empty space - if it is nakade or just a play in a similar shape in an
already safe group.
RETURNS estimate of size of group in nakade if play is potential nakade, or 0
*/
u8 is_nakade(
    const cfg_board * cb,
    move m
);

/*
Marks intersections near point m. The definition of near used includes
intersections adjacent to liberties of nearby groups, plus the 3x3 neighborhood
of the intersection m. near_pos is cleared before marking.
*/
void mark_near_pos(
    bool near_pos[BOARD_SIZ * BOARD_SIZ],
    const cfg_board * cb,
    move m
);

/*
Marks points in seki in the whole board. Only catches simple sekis with two
shared liberties or one shared liberty and two eyes, that have the same or
almost the same number of stones.
*/
void mark_pts_in_seki(
    bool in_seki[BOARD_SIZ * BOARD_SIZ],
    cfg_board * cb
);

/*
Tests if the intersection m is the only liberty of a group of color is_black and
that group qualifies as being a ladder.
Warning: assumes playing m makes two and only two liberties.

Not all types of ladders are detected.

It applies to simple ladders like (O = part of group in atari that
can't survive by capturing; X = opponent; 1 is Os liberty)
. . . . . . . . |
. . . . O . . . |
. . . X 1 3 4 . |
. . . . 2 6 8 9 |
. . . . . 7 0 2 |
. . . . . . 1 . |
. . . . . . . . |

Does not consider ladders like:
. . . . . .
. b b b b .
. b w w b .
. b w ! . .
. b b . . .
. . . . . .
*/
bool is_ladder(
    const cfg_board * cb,
    move m,
    bool is_black
);

/*
Return the minimum amount of liberties of groups with stones adjacent to an
intersection.
RETURNS minimum number of liberties found, or NONE
*/
u16 min_neighbor_libs(
    const cfg_board * cb,
    move m,
    u8 stone
);

/*
Return the maximum amount of liberties of groups with stones adjacent to an
intersection.
RETURNS maximum number of liberties found, or 0
*/
u8 max_neighbor_libs(
    const cfg_board * cb,
    move m,
    u8 stone
);

/*
Count the size of the empty space in an empty area.
RETURNS true if larger than two intersections
*/
bool eye_space_size_gt_two(
    const cfg_board * cb,
    move m
);

/*
Tests whether a neighbor group of stone type stone has two liberties.
RETURNS true if neighbor group is put in atari
*/
bool puts_neighbor_in_atari(
    const cfg_board * cb,
    move m,
    u8 stone
);

/*
Return the maximum number of stones of a group of stones of value stone;
adjacent to the intersection m.
RETURNS maximum number of stones of a group, or 0
*/
u16 max_neighbor_group_stones(
    const cfg_board * cb,
    move m,
    u8 stone
);

/*
Tests whether two groups have exactly the same liberties.
RETURNS true if the groups have the exact same liberties
*/
bool groups_same_liberties(
    const group * restrict g1,
    const group * restrict g2
);

/*
Tests whether two groups share at least one liberty.
RETURNS true if the groups share at least one liberty
*/
bool groups_share_liberties(
    const group * restrict g1,
    const group * restrict g2
);

/*
Counts the number of shared liberties between two groups.
RETURNS number of shared liberties
*/
u8 groups_shared_liberties(
    const group * restrict g1,
    const group * restrict g2
);

/*
Tests whether group g can be attacked and eventually killed by its opponent,
with no chance of making at least three liberties.
RETURNS play that ensures the group is killed, or NONE
*/
move can_be_killed(
    const cfg_board * cb,
    const group * g
);

/*
Tests whether group g can be attacked and eventually killed by its opponent,
with no chance of making at least three liberties.
Saves all killing plays in plays array at index plays_count, increasing it.
*/
void can_be_killed_all(
    const cfg_board * cb,
    const group * g,
    u16 * plays_count,
    move * plays
);

/*
Tests whether group g can be led to have at least three liberties regardless of
the opponent attack. None is also returned if the saving play is to pass.
RETURNS one play that saves the group from being killed, or NONE
*/
move can_be_saved(
    const cfg_board * cb,
    const group * g
);

/*
Tests whether group g can be led to have at least three liberties regardless of
the opponent attack. Saves all saving plays in plays array at index plays_count,
increasing it.
*/
void can_be_saved_all(
    const cfg_board * cb,
    const group * g,
    u16 * plays_count,
    move * plays
);


#endif

