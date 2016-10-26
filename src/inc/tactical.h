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
An eye is a point that may eventually become untakeable (without playing
at the empty intersection itself). Examples:

.bw   .b.   ---   +--
b*b   b*b   b*b   |*b
.bb   .bb   .b.   |b.

RETURNS true if coordinate is that of an eye
*/
bool is_eye(
    const cfg_board * cb,
    bool is_black,
    move m
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
    bool is_black,
    move m,
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
    bool is_black,
    move m,
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
    bool is_black,
    move m
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
Test if play is in an apparently safe tigers mouth.
RETURNS true if inside tigers mouth
*/
bool safe_tigers_mouth(
    const cfg_board * cb,
    bool is_black,
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
    bool near_pos[TOTAL_BOARD_SIZ],
    const cfg_board * cb,
    move m
);

/*
Tests whether group g can be attacked and eventually killed by its opponent,
with no chance of making at least three liberties.
RETURNS play that ensures the group is killed, or NONE
*/
move get_killing_play(
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
Assuming the group is in danger, attempts to find a play that will produce at
least three liberties, regardless of opponent play.
RETURNS  play that saves the group from being killed, or NONE
*/
move get_saving_play(
    const cfg_board * cb,
    const group * g
);

/*
Tests whether group g can be led to have at least three liberties regardless of
opponent play.
RETURNS true if can be made to have at least three liberties
*/
bool can_be_saved(
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

