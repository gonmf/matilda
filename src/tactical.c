/*
Tactical functions that make use of a cfg_board structure.

Tactical analysis in Matilda mostly concerns with two things:
- Eye shape -- eyes, nakade
- Life and death -- ladders, seki, 1-2 liberty solvers for killing and saving
groups, connecting groups by kosumi, bamboo joints, etc for the
purpose of eye counting.
*/

#include "matilda.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <omp.h>

#include "board.h"
#include "cfg_board.h"
#include "move.h"
#include "state_changes.h"
#include "types.h"


extern u8 out_neighbors8[BOARD_SIZ * BOARD_SIZ];
extern u8 out_neighbors4[BOARD_SIZ * BOARD_SIZ];
extern move_seq neighbors_3x3[BOARD_SIZ * BOARD_SIZ];

extern bool border_left[BOARD_SIZ * BOARD_SIZ];
extern bool border_right[BOARD_SIZ * BOARD_SIZ];
extern bool border_top[BOARD_SIZ * BOARD_SIZ];
extern bool border_bottom[BOARD_SIZ * BOARD_SIZ];

/* number of active bits for every byte combination */
extern u8 dyn_active_bits[256];


/*
RETURNS true if move coordinate is that of an eye
*/
bool is_eye(
    const cfg_board * cb,
    move m,
    bool is_black
){
    if(out_neighbors4[m] == 0)
    {
        if(is_black)
            return (cb->black_neighbors4[m] == 4) && (cb->black_neighbors8[m] >
                6);
        else
            return (cb->white_neighbors4[m] == 4) && (cb->white_neighbors8[m] >
                6);
    }
    else
    {
        if(is_black)
           return (cb->black_neighbors8[m] + out_neighbors8[m]) == 8;
        else
            return (cb->white_neighbors8[m] + out_neighbors8[m]) == 8;
    }
}

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
){
    if(out_neighbors4[m] == 0)
    {
        if(is_black)
            return (cb->black_neighbors4[m] == 4) && (cb->white_neighbors8[m] <
                2);
        else
            return (cb->white_neighbors4[m] == 4) && (cb->black_neighbors8[m] <
                2);
    }
    else
    {
        if(is_black)
            return (cb->black_neighbors4[m] + out_neighbors4[m] == 4) &&
                (cb->white_neighbors8[m] == 0);
        else
            return (cb->white_neighbors4[m] + out_neighbors4[m] == 4) &&
                (cb->black_neighbors8[m] == 0);
    }
}

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
){
    assert(is_board_move(m));
    *can_have_forcing_move = false;

    if(is_black)
    {
        if(cb->white_neighbors4[m] > 0)
            return false;
        if(cb->black_neighbors4[m] + out_neighbors4[m] != 3)
            return false;

        bool strict = (out_neighbors4[m] > 0);
        move m2;
        if(!border_right[m] && cb->p[m + RIGHT] == EMPTY)
            m2 = m + RIGHT;
        else
            if(!border_bottom[m] && cb->p[m + BOTTOM] == EMPTY)
                m2 = m + BOTTOM;
            else
                return false;

        strict |= (out_neighbors4[m2] > 0);

        if(cb->black_neighbors4[m2] + out_neighbors4[m2] != 3)
            return false;

        if(strict)
        {
            if(cb->black_neighbors8[m] + out_neighbors8[m] < 7)
                return false;
            if(cb->black_neighbors8[m2] + out_neighbors8[m2] < 7)
                return false;
        }
        else
        {
            u8 nm1 = cb->black_neighbors8[m] + out_neighbors8[m];
            if(nm1 < 6)
                return false;
            u8 nm2 = cb->black_neighbors8[m2] + out_neighbors8[m2];
            if(nm2 < 6)
                return false;
            if(nm1 + nm2 == 12)
                *can_have_forcing_move = true;
        }
    }
    else
    {
        if(cb->black_neighbors4[m] > 0)
            return false;
        if(cb->white_neighbors4[m] + out_neighbors4[m] != 3)
            return false;

        bool strict = (out_neighbors4[m] > 0);
        move m2;

        if(!border_right[m] && cb->p[m + RIGHT] == EMPTY)
            m2 = m + RIGHT;
        else
            if(!border_bottom[m] && cb->p[m + BOTTOM] == EMPTY)
                m2 = m + BOTTOM;
            else
                return false;

        strict |= (out_neighbors4[m2] > 0);

        if(cb->white_neighbors4[m2] + out_neighbors4[m2] != 3)
            return false;

        if(strict)
        {
            if(cb->white_neighbors8[m] + out_neighbors8[m] < 7)
                return false;
            if(cb->white_neighbors8[m2] + out_neighbors8[m2] < 7)
                return false;
        }
        else
        {
            u8 nm1 = cb->white_neighbors8[m] + out_neighbors8[m];
            if(nm1 < 6)
                return false;
            u8 nm2 = cb->white_neighbors8[m2] + out_neighbors8[m2];
            if(nm2 < 6)
                return false;
            if(nm1 + nm2 == 12)
                *can_have_forcing_move = true;
        }
    }

    return true;
}


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
){
    assert(is_board_move(m));

    if(border_right[m] || border_bottom[m])
        return false;
    if(cb->p[m + RIGHT] != EMPTY || cb->p[m + BOTTOM] != EMPTY || cb->p[m +
        BOTTOM + RIGHT] != EMPTY)
        return false;

    if(out_neighbors4[m] == 0 && out_neighbors4[m + BOTTOM + RIGHT] == 0)
    {
        if(is_black)
        {
            if(cb->black_neighbors4[m] != 2)
                return false;
            if(cb->black_neighbors4[m + RIGHT] != 2)
                return false;
            if(cb->black_neighbors4[m + BOTTOM] != 2)
                return false;
            if(cb->black_neighbors4[m + BOTTOM + RIGHT] != 2)
                return false;

            u8 sum = cb->black_neighbors8[m] + cb->black_neighbors8[m + RIGHT] +
                cb->black_neighbors8[m + BOTTOM] + cb->black_neighbors8[m +
                BOTTOM + RIGHT];

            if(sum < 18)
                return false;

            *can_have_forcing_move = (sum == 18);
        }
        else
        {
            if(cb->white_neighbors4[m] != 2)
                return false;
            if(cb->white_neighbors4[m + RIGHT] != 2)
                return false;
            if(cb->white_neighbors4[m + BOTTOM] != 2)
                return false;
            if(cb->white_neighbors4[m + BOTTOM + RIGHT] != 2)
                return false;

            u8 sum = cb->white_neighbors8[m] + cb->white_neighbors8[m + RIGHT] +
                cb->white_neighbors8[m + BOTTOM] + cb->white_neighbors8[m +
                BOTTOM + RIGHT];

            if(sum < 18)
                return false;

            *can_have_forcing_move = (sum == 18);
        }
    }
    else
    {
        if(is_black)
        {
            if(cb->black_neighbors8[m] + out_neighbors8[m] != 5)
                return false;
            if(cb->black_neighbors8[m + RIGHT] + out_neighbors8[m + RIGHT] != 5)
                return false;
            if(cb->black_neighbors8[m + BOTTOM] + out_neighbors8[m + BOTTOM] !=
                5)
                return false;
            if(cb->black_neighbors8[m + BOTTOM + RIGHT] + out_neighbors8[m +
                BOTTOM + RIGHT] != 5)
                return false;
        }
        else
        {
            if(cb->white_neighbors8[m] + out_neighbors8[m] != 5)
                return false;
            if(cb->white_neighbors8[m + RIGHT] + out_neighbors8[m + RIGHT] != 5)
                return false;
            if(cb->white_neighbors8[m + BOTTOM] + out_neighbors8[m + BOTTOM] !=
                5)
                return false;
            if(cb->white_neighbors8[m + BOTTOM + RIGHT] + out_neighbors8[m +
                BOTTOM + RIGHT] != 5)
                return false;
        }
        *can_have_forcing_move = false;
    }

    return true;
}

/*
A point surrounded by stones of the same color. It is not a safe eye but if
being the connector between groups with at least one independent liberty, it
becomes an eye.
RETURNS true if m is a connecting liberty
*/
bool sheltered_liberty(
    const cfg_board * cb,
    move m
){
    return (cb->black_neighbors4[m] + out_neighbors4[m] == 4) ||
        (cb->white_neighbors4[m] + out_neighbors4[m] == 4);
}

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
){
    if(is_black)
    {
        if(cb->white_neighbors8[m] > 0)
            return false;
        u8 on4 = cb->black_neighbors4[m] + out_neighbors4[m];
        if(on4 != 3)
            return false;
        u8 on8 = cb->black_neighbors8[m] + out_neighbors8[m];
        return on8 == 7;
    }
    else
    {
        if(cb->black_neighbors8[m] > 0)
            return false;
        u8 on4 = cb->white_neighbors4[m] + out_neighbors4[m];
        if(on4 != 3)
            return false;
        u8 on8 = cb->white_neighbors8[m] + out_neighbors8[m];
        return on8 == 7;
    }
}


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
){
    if(border_right[m] || border_top[m] || border_bottom[m])
        return false;
    u8 o = cb->p[m + TOP];
    if(o == EMPTY)
        return false;
    return cb->p[m] == EMPTY && cb->p[m + RIGHT] == EMPTY && cb->p[m + RIGHT +
        TOP] == o && cb->p[m + BOTTOM] == o && cb->p[m + RIGHT + BOTTOM] == o;
}

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
){
    if(border_left[m] || border_right[m] || border_bottom[m])
        return false;
    u8 o = cb->p[m + LEFT];
    if(o == EMPTY)
        return false;
    return cb->p[m] == EMPTY && cb->p[m + BOTTOM] == EMPTY && cb->p[m + LEFT +
        BOTTOM] == o && cb->p[m + RIGHT] == o && cb->p[m + RIGHT + BOTTOM] == o;
}

/*
Test if play is in an apparently safe tigers mouth.
RETURNS true if inside tigers mouth
*/
bool safe_tigers_mouth(
    const cfg_board * cb,
    bool is_black,
    move m
){
    if(is_black)
        return (out_neighbors4[m] == 0 && cb->white_neighbors4[m] == 0 &&
            cb->black_neighbors4[m] >= 3 && cb->white_neighbors8[m] <= 1);

    return (out_neighbors4[m] == 0 && cb->black_neighbors4[m] == 0 &&
            cb->white_neighbors4[m] >= 3 && cb->black_neighbors8[m] <= 1);
}

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
){
    if(border_right[m] || border_bottom[m])
        return false;
    u8 o = cb->p[m + RIGHT];
    if(o == EMPTY)
        return false;
    return cb->p[m + BOTTOM] == o && cb->p[m + RIGHT + BOTTOM] == EMPTY;
}

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
){
    if(border_left[m] || border_bottom[m])
        return false;
    u8 o = cb->p[m + LEFT];
    if(o == EMPTY)
        return false;
    return cb->p[m + BOTTOM] == o && cb->p[m + LEFT + BOTTOM] == EMPTY;
}

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
){
    assert(is_board_move(m));

    if((cb->black_neighbors8[m] > 0) == (cb->white_neighbors8[m] > 0))
        return 0;

    if(cb->black_neighbors8[m] > 0){
        u8 on4 = cb->black_neighbors4[m] + out_neighbors4[m];
        u8 on8 = cb->black_neighbors8[m] + out_neighbors8[m];
        if((on4 < 3) && (on8 == on4 + 4))
        {
            /*
            Straight three, bent three, pyramid four or crossed five
            */
            if(!border_left[m] && cb->p[m + LEFT] == EMPTY)
            {
                if(cb->black_neighbors4[m + LEFT] + out_neighbors4[m + LEFT] !=
                    3)
                    return 0;
                if((out_neighbors4[m + LEFT] == 0 && cb->white_neighbors8[m +
                    LEFT] > 1) || (out_neighbors4[m + LEFT] > 0 &&
                    cb->white_neighbors8[m + LEFT] > 0))
                    return 0;
            }
            if(!border_right[m] && cb->p[m + RIGHT] == EMPTY)
            {
                if(cb->black_neighbors4[m + RIGHT] + out_neighbors4[m + RIGHT]
                    != 3)
                    return 0;
                if((out_neighbors4[m + RIGHT] == 0 && cb->white_neighbors8[m +
                    RIGHT] > 1) || (out_neighbors4[m + RIGHT] > 0 &&
                    cb->white_neighbors8[m + RIGHT] > 0))
                    return 0;
            }
            if(!border_top[m] && cb->p[m + TOP] == EMPTY)
            {
                if(cb->black_neighbors4[m + TOP] + out_neighbors4[m + TOP] != 3)
                return 0;
                    if((out_neighbors4[m + TOP] == 0 && cb->white_neighbors8[m +
                        TOP] > 1) || (out_neighbors4[m + TOP] > 0 &&
                        cb->white_neighbors8[m + TOP] > 0))
                return 0;
            }
            if(!border_bottom[m] && cb->p[m + BOTTOM] == EMPTY)
            {
                if(cb->black_neighbors4[m + BOTTOM] + out_neighbors4[m + BOTTOM]
                    != 3)
                    return 0;
                if((out_neighbors4[m + BOTTOM] == 0 &&
                    cb->white_neighbors8[m + BOTTOM] > 1) ||
                    (out_neighbors4[m + BOTTOM] > 0 &&
                    cb->white_neighbors8[m + BOTTOM] > 0))
                    return 0;
            }
            return (4 - on4) * 4 + 4;
        }
        else
        {
            if(on4 < 2 && on8 == on4 + 3)
            {
                /*
                Bulky five or rabbity six
                */
                u8 near_corner = 0;
                if(!border_left[m] && cb->p[m + LEFT] == EMPTY)
                {
                    u8 n4 = cb->black_neighbors4[m + LEFT] + out_neighbors4[m +
                        LEFT];
                    if(n4 == 2)
                    {
                        ++near_corner;

                        if(cb->white_neighbors8[m + LEFT] > 0)
                            return 0;
                        if(cb->black_neighbors8[m + LEFT] + out_neighbors8[m +
                            LEFT] != 4)
                            return 0;
                    }
                    else
                    {
                        if(n4 != 3)
                            return 0;

                        if((out_neighbors4[m + LEFT] == 0 &&
                            cb->white_neighbors8[m + LEFT] > 1) ||
                            (out_neighbors4[m + LEFT] > 0 &&
                            cb->white_neighbors8[m + LEFT] > 0))
                            return 0;
                    }
                }
                if(!border_right[m] && cb->p[m + RIGHT] == EMPTY)
                {
                    u8 n4 = cb->black_neighbors4[m + RIGHT] + out_neighbors4[m +
                        RIGHT];
                    if(n4 == 2){
                        ++near_corner;

                        if(cb->white_neighbors8[m + RIGHT] > 0)
                            return 0;
                        if(cb->black_neighbors8[m + RIGHT] + out_neighbors8[m +
                            RIGHT] != 4)
                            return 0;
                    }
                    else
                    {
                        if(n4 != 3)
                            return 0;

                        if((out_neighbors4[m + RIGHT] == 0 &&
                            cb->white_neighbors8[m + RIGHT] > 1) ||
                            (out_neighbors4[m + RIGHT] > 0 &&
                            cb->white_neighbors8[m + RIGHT] > 0))
                            return 0;
                    }
                }
                if(!border_top[m] && cb->p[m + TOP] == EMPTY)
                {
                    u8 n4 = cb->black_neighbors4[m + TOP] + out_neighbors4[m +
                        TOP];
                    if(n4 == 2)
                    {
                        if(near_corner == 2)
                            return 0;
                        ++near_corner;

                        if(cb->white_neighbors8[m + TOP] > 0)
                            return 0;
                        if(cb->black_neighbors8[m + TOP] + out_neighbors8[m +
                            TOP] != 4)
                            return 0;
                    }
                    else
                    {
                        if(n4 != 3)
                            return 0;

                        if((out_neighbors4[m + TOP] == 0 &&
                            cb->white_neighbors8[m + TOP] > 1) ||
                            (out_neighbors4[m + TOP] > 0 &&
                            cb->white_neighbors8[m + TOP] > 0))
                            return 0;
                    }
                }
                if(!border_bottom[m] && cb->p[m + BOTTOM] == EMPTY)
                {
                    u8 n4 = cb->black_neighbors4[m + BOTTOM] + out_neighbors4[m
                        + BOTTOM];
                    if(n4 == 2)
                    {
                        if(near_corner == 2)
                            return 0;
                        ++near_corner;

                        if(cb->white_neighbors8[m + BOTTOM] > 0)
                            return 0;
                        if(cb->black_neighbors8[m + BOTTOM] + out_neighbors8[m +
                            BOTTOM] != 4)
                            return 0;
                    }
                    else
                    {
                        if(n4 != 3)
                            return 0;

                        if((out_neighbors4[m + BOTTOM] == 0 &&
                            cb->white_neighbors8[m + BOTTOM] > 1) ||
                            (out_neighbors4[m + BOTTOM] > 0 &&
                            cb->white_neighbors8[m + BOTTOM] > 0))
                            return 0;
                    }
                }

                if(near_corner != 2)
                    return 0;

                return (5 - on4) * 5;
            }
        }
    }
    else
    {
        u8 on4 = cb->white_neighbors4[m] + out_neighbors4[m];
        u8 on8 = cb->white_neighbors8[m] + out_neighbors8[m];
        if((on4 < 3) && (on8 == on4 + 4))
        {
            /*
            Straight three, bent three, pyramid four or crossed five
            */
            if(!border_left[m] && cb->p[m + LEFT] == EMPTY){
                if(cb->white_neighbors4[m + LEFT] + out_neighbors4[m + LEFT] !=
                    3)
                    return 0;
                if((out_neighbors4[m + LEFT] == 0 && cb->black_neighbors8[m +
                    LEFT] > 1) || (out_neighbors4[m + LEFT] > 0 &&
                    cb->black_neighbors8[m + LEFT] > 0))
                    return 0;
            }
            if(!border_right[m] && cb->p[m + RIGHT] == EMPTY){
                if(cb->white_neighbors4[m + RIGHT] + out_neighbors4[m + RIGHT]
                    != 3)
                    return 0;
                if((out_neighbors4[m + RIGHT] == 0 && cb->black_neighbors8[m +
                    RIGHT] > 1) || (out_neighbors4[m + RIGHT] > 0 &&
                    cb->black_neighbors8[m + RIGHT] > 0))
                    return 0;
            }
            if(!border_top[m] && cb->p[m + TOP] == EMPTY){
                if(cb->white_neighbors4[m + TOP] + out_neighbors4[m + TOP] != 3)
                    return 0;
                if((out_neighbors4[m + TOP] == 0 && cb->black_neighbors8[m +
                    TOP] > 1) || (out_neighbors4[m + TOP] > 0 &&
                    cb->black_neighbors8[m + TOP] > 0))
                    return 0;
            }
            if(!border_bottom[m] && cb->p[m + BOTTOM] == EMPTY){
                if(cb->white_neighbors4[m + BOTTOM] + out_neighbors4[m + BOTTOM]
                    != 3)
                    return 0;
                if((out_neighbors4[m + BOTTOM] == 0 && cb->black_neighbors8[m +
                    BOTTOM] > 1) || (out_neighbors4[m + BOTTOM] > 0 &&
                    cb->black_neighbors8[m + BOTTOM] > 0))
                    return 0;
            }
            return (4 - on4) * 4 + 4;
        }
        else
        {
            if(on4 < 2 && on8 == on4 + 3)
            {
                /*
                Bulky five or rabbity six
                */
                u8 near_corner = 0;
                if(!border_left[m] && cb->p[m + LEFT] == EMPTY)
                {
                    u8 n4 = cb->white_neighbors4[m + LEFT] + out_neighbors4[m +
                        LEFT];
                    if(n4 == 2)
                    {
                        ++near_corner;

                        if(cb->black_neighbors8[m + LEFT] > 0)
                            return 0;
                        if(cb->white_neighbors8[m + LEFT] + out_neighbors8[m +
                            LEFT] != 4)
                            return 0;
                    }
                    else
                    {
                        if(n4 != 3)
                            return 0;

                        if((out_neighbors4[m + LEFT] == 0 &&
                            cb->black_neighbors8[m + LEFT] > 1) ||
                            (out_neighbors4[m + LEFT] > 0 &&
                            cb->black_neighbors8[m + LEFT] > 0))
                            return 0;
                    }
                }
                if(!border_right[m] && cb->p[m + RIGHT] == EMPTY)
                {
                    u8 n4 = cb->white_neighbors4[m + RIGHT] + out_neighbors4[m +
                        RIGHT];
                    if(n4 == 2)
                    {
                        ++near_corner;

                        if(cb->black_neighbors8[m + RIGHT] > 0)
                            return 0;
                        if(cb->white_neighbors8[m + RIGHT] + out_neighbors8[m +
                            RIGHT] != 4)
                            return 0;
                    }
                    else
                    {
                        if(n4 != 3)
                            return 0;

                        if((out_neighbors4[m + RIGHT] == 0 &&
                            cb->black_neighbors8[m + RIGHT] > 1) ||
                            (out_neighbors4[m + RIGHT] > 0 &&
                            cb->black_neighbors8[m + RIGHT] > 0))
                            return 0;
                    }
                }
                if(!border_top[m] && cb->p[m + TOP] == EMPTY){
                    u8 n4 = cb->black_neighbors4[m + TOP] + out_neighbors4[m +
                        TOP];
                    if(n4 == 2)
                    {
                        if(near_corner == 2)
                            return 0;
                        ++near_corner;

                        if(cb->black_neighbors8[m + TOP] > 0)
                            return 0;
                        if(cb->white_neighbors8[m + TOP] + out_neighbors8[m +
                            TOP] != 4)
                            return 0;
                    }
                    else
                    {
                        if(n4 != 3)
                            return 0;

                        if((out_neighbors4[m + TOP] == 0 &&
                            cb->black_neighbors8[m + TOP] > 1) ||
                            (out_neighbors4[m + TOP] > 0 &&
                            cb->black_neighbors8[m + TOP] > 0))
                            return 0;
                    }
                }
                if(!border_bottom[m] && cb->p[m + BOTTOM] == EMPTY)
                {
                    u8 n4 = cb->black_neighbors4[m + BOTTOM] + out_neighbors4[m
                        + BOTTOM];
                    if(n4 == 2)
                    {
                        if(near_corner == 2)
                            return 0;
                        ++near_corner;

                        if(cb->black_neighbors8[m + BOTTOM] > 0)
                            return 0;
                        if(cb->white_neighbors8[m + BOTTOM] + out_neighbors8[m +
                            BOTTOM] != 4)
                            return 0;
                    }
                    else
                    {
                        if(n4 != 3)
                            return 0;

                        if((out_neighbors4[m + BOTTOM] == 0 &&
                            cb->black_neighbors8[m + BOTTOM] > 1) ||
                            (out_neighbors4[m + BOTTOM] > 0 &&
                            cb->black_neighbors8[m + BOTTOM] > 0))
                            return 0;
                    }
                }

                if(near_corner != 2)
                    return 0;

                return (5 - on4) * 5;
            }
        }
    }
    return 0;
}

/*
Marks intersections near point m. The definition of near used includes
intersections adjacent to liberties of nearby groups, plus the 3x3 neighborhood
of the intersection m. near_pos is cleared before marking.
*/
void mark_near_pos(
    bool near_pos[BOARD_SIZ * BOARD_SIZ],
    const cfg_board * cb,
    move m
){
    memset(near_pos, false, BOARD_SIZ * BOARD_SIZ);
    assert(is_board_move(m));

    /* Fill a square around (x,y) */
    for(move k = 0; k < neighbors_3x3[m].count; ++k)
    {
        move n = neighbors_3x3[m].coord[k];
        near_pos[n] = true;
    }

    group * g = cb->g[m];
    if(g != NULL)
    {
        u8 libs = g->liberties;
        for(move n = g->liberties_min_coord; libs > 0; ++n)
        {
            u8 mask = (1 << (n % 8));
            if(g->ls[n / 8] & mask)
            {
                near_pos[n] = true;
                --libs;
            }
        }
    }
}

/*
Marks points in seki in the whole board. Only catches simple sekis with two
shared liberties or one shared liberty and two eyes, that have the same or
almost the same number of stones.
*/
void mark_pts_in_seki(
    bool in_seki[BOARD_SIZ * BOARD_SIZ],
    cfg_board * cb
){
    /*
    Discover unique groups for one of the players
    */
    group * black_groups[MAX_GROUPS];
    u16 black_groups_count = 0;

    for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
        if(cb->g[m] != NULL && cb->g[m]->is_black && cb->g[m]->liberties == 2)
        {
            bool found = false;
            for(u16 k = 0; k < black_groups_count; ++k)
                if(cb->g[m] == black_groups[k])
                {
                    found = true;
                    break;
                }
            if(!found)
            {
                black_groups[black_groups_count] = cb->g[m];
                ++black_groups_count;
            }
        }

    /*
    For each group check if it is in semeai with a neighbor
    */
    for(u16 i = 0; i < black_groups_count; ++i)
    {
        group * g = black_groups[i];
        move not_eye = NONE;
        u8 eye_libs = 0;
        u8 libs = 0;
        for(move m = g->liberties_min_coord; m < BOARD_SIZ * BOARD_SIZ; ++m)
        {
            u8 mask = (1 << (m % 8));
            if(g->ls[m / 8] & mask)
            {
                if(is_eye(cb, m, true))
                    ++eye_libs;
                else
                {
                    bool caps;
                    if(safe_to_play(cb, m, true, &caps) == 1 && safe_to_play(cb,
                        m, false, &caps) == 1)
                        not_eye = m;
                    else
                        goto next_group;
                }
                ++libs;
                if(libs == 2)
                    break;
            }
        }

        /*
        If both liberties are eyes we cannot be in seki
        */
        if(eye_libs == 2)
            continue;
        assert(eye_libs < 2);

        if(eye_libs == 1)
        {
            /*
            Possible one shared / two eyes seki
            */
            for(u8 j = 0; j < g->neighbors_count; ++j)
            {
                group * n = cb->g[g->neighbors[j]];
                if(n->liberties != 2 || g->stones.count != n->stones.count)
                    continue;

                bool share_same_liberty = false;
                for(move m = n->liberties_min_coord; m < BOARD_SIZ * BOARD_SIZ;
                    ++m)
                {
                    u8 mask = (1 << (m % 8));
                    if(n->ls[m / 8] & mask)
                    {
                        if(m == not_eye)
                        {
                            /*
                            We share the same shared liberty so we can be in
                            seki; but it has to be a self-atari to play there
                            */
                            bool caps;
                            if(safe_to_play(cb, m, true, &caps) == 1 &&
                                safe_to_play(cb, m, false, &caps) == 1)
                            {
                                /* not marking the eyes because they are, well,
                                eyes already */
                                share_same_liberty = true;
                            }else{
                                /*
                                We're not in seki after all
                                */
                                goto next_neighbor;
                            }

                        }
                        else
                        {
                            /*
                            Other liberty has to be an eye for this to be safely
                            a seki
                            */
                            if(!is_eye(cb, m, false))
                                goto next_neighbor;
                        }
                    }
                }

                if(share_same_liberty)
                {
                    in_seki[not_eye] = true;
                    break;
                }
                next_neighbor: ;
            }
        }
        else
        {
            /*
            Possible two shared libs seki
            */
            for(u8 j = 0; j < g->neighbors_count; ++j)
            {
                group * n = cb->g[g->neighbors[j]];
                if(n->liberties != 2 || g->stones.count != n->stones.count)
                    continue;
                if(memcmp(g->ls, n->ls, LIB_BITMAP_SIZ) != 0)
                    continue;

                /*
                Since we have the same two liberties, and already tested them
                prior, we just have to mark them now
                */
                u8 libs2 = 0;
                for(move m = g->liberties_min_coord; m < BOARD_SIZ * BOARD_SIZ;
                    ++m)
                {
                    u8 mask = (1 << (m % 8));
                    if(g->ls[m / 8] & mask)
                    {
                        in_seki[m] = true;
                        ++libs2;
                        if(libs2 == 2)
                            goto next_group;
                    }
                }
            }
        }
        next_group: ;
    }
}

static void move_dir(
    move * m,
    d8 dir[2]
){
    *m = *m + dir[0] + dir[1] * BOARD_SIZ;
}

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
){
    /* We're to close to the border */
    if(out_neighbors4[m] > 0)
        return false;

    /* Group liberty has unexpected number of friendly neighbors */
    if((is_black && (cb->black_neighbors4[m] != 1)) || (!is_black &&
        (cb->white_neighbors4[m] != 1)))
        return false;

    /* Group liberty has too few unfriendly neighbors */
    if((is_black && (cb->white_neighbors4[m] == 0)) || (!is_black &&
        (cb->black_neighbors4[m] == 0)))
        return false;

    /* Discover the closest group of is_black color */
    group * g;
    group * n;
    if(!border_left[m] && (n = cb->g[m + LEFT]) != NULL && n->is_black ==
        is_black)
        g = n;
    else
        if(!border_right[m] && (n = cb->g[m + RIGHT]) != NULL && n->is_black ==
            is_black)
            g = n;
        else
            if(!border_top[m] && (n = cb->g[m + TOP]) != NULL && n->is_black ==
                is_black)
                g = n;
            else
                g = cb->g[m + BOTTOM];

    /* Group not in atari */
    if(g->liberties != 1)
        return false;

    /*
    If group makes libs by capture then we are not a ladder yet
    Warning: not considering ko
    */
    for(u16 k = 0; k < g->neighbors_count; ++k)
        if(cb->g[g->neighbors[k]]->liberties == 1)
            return false;

    /* If one of the diagonals of the liberty only has 2 libs, then it can be
    captured if the ladder is chased */
    if(!border_left[m])
    {
        if(!border_top[m] && (n = cb->g[m + LEFT + TOP]) != NULL && n->is_black
            != is_black && n->liberties <= 2)
            return false;
        if(!border_bottom[m] && (n = cb->g[m + LEFT + BOTTOM]) != NULL &&
            n->is_black != is_black && n->liberties <= 2)
            return false;
    }

    if(!border_right[m])
    {
        if(!border_top[m] && (n = cb->g[m + RIGHT + TOP]) != NULL && n->is_black
            != is_black && n->liberties <= 2)
            return false;
        if(!border_bottom[m] && (n = cb->g[m + RIGHT + BOTTOM]) != NULL &&
            n->is_black != is_black && n->liberties <= 2)
            return false;
    }

    d8 dir1[2]; /* direction vector 1 */

    /* direction away from neighbor friendly stone */
    if(!border_left[m] && cb->g[m + LEFT] == g)
    {
        dir1[0] = 1;
        dir1[1] = 0;
    }
    else
        if(!border_right[m] && cb->g[m + RIGHT] == g)
        {
            dir1[0] = -1;
            dir1[1] = 0;
        }
        else
            if(!border_top[m] && cb->g[m + TOP] == g)
            {
                dir1[0] = 0;
                dir1[1] = 1;
            }
            else
            {
                dir1[0] = 0;
                dir1[1] = -1;
            }

    move_dir(&m, dir1);
    if((is_black && (cb->black_neighbors4[m] > 1)) || (!is_black &&
        (cb->white_neighbors4[m] > 1)))
        return false;
    if(out_neighbors4[m] > 0)
        return true;

    d8 dir2[2]; /* direction vector 2 */

    /* direction away from imaginary side stone */
    dir2[0] = dir1[1];
    dir2[1] = dir1[0];
    move m2 = m;
    move_dir(&m2, dir2);
    if(cb->p[m2] != EMPTY)
    {
        /* we're going the wrong direction */
        dir2[0] *= -1;
        dir2[1] *= -1;
    }

    move_dir(&m, dir2);
    if((is_black && (cb->black_neighbors8[m] > 1)) || (!is_black &&
        (cb->white_neighbors8[m] > 1)))
        return false;
    if(out_neighbors4[m] > 0)
        return true;

    while(1)
    {
        move_dir(&m, dir1);
        if((is_black && (cb->black_neighbors8[m] > 0)) || (!is_black &&
            (cb->white_neighbors8[m] > 0)))
            return false;
        if(out_neighbors4[m] > 0)
            return true;
        move_dir(&m, dir2);
        if((is_black && (cb->black_neighbors8[m] > 0)) || (!is_black &&
            (cb->white_neighbors8[m] > 0)))
            return false;
        if(out_neighbors4[m] > 0)
            return true;
    }

    assert(0);
}


/*
Return the minimum amount of liberties of groups with stones adjacent to an
intersection.
RETURNS minimum number of liberties found, or NONE
*/
u16 min_neighbor_libs(
    const cfg_board * cb,
    move m,
    u8 stone
){
    assert(is_board_move(m));

    u16 ret = NONE;
    if(!border_left[m] && cb->p[m + LEFT] == stone)
        ret = cb->g[m + LEFT]->liberties;
    if(!border_right[m] && cb->p[m + RIGHT] == stone && cb->g[m +
        RIGHT]->liberties < ret)
        ret = cb->g[m + RIGHT]->liberties;
    if(!border_top[m] && cb->p[m + TOP] == stone && cb->g[m + TOP]->liberties <
        ret)
        ret = cb->g[m + TOP]->liberties;
    if(!border_bottom[m] && cb->p[m + BOTTOM] == stone && cb->g[m +
        BOTTOM]->liberties < ret)
        ret = cb->g[m + BOTTOM]->liberties;
    return ret;
}


/*
Return the maximum amount of liberties of groups with stones adjacent to an
intersection.
RETURNS maximum number of liberties found, or 0
*/
u8 max_neighbor_libs(
    const cfg_board * cb,
    move m,
    u8 stone
){
    assert(is_board_move(m));

    u8 ret = 0;
    if(!border_left[m] && cb->p[m + LEFT] == stone)
        ret = cb->g[m + LEFT]->liberties;
    if(!border_right[m] && cb->p[m + RIGHT] == stone && cb->g[m +
        RIGHT]->liberties > ret)
        ret = cb->g[m + RIGHT]->liberties;
    if(!border_top[m] && cb->p[m + TOP] == stone && cb->g[m + TOP]->liberties >
        ret)
        ret = cb->g[m + TOP]->liberties;
    if(!border_bottom[m] && cb->p[m + BOTTOM] == stone && cb->g[m +
        BOTTOM]->liberties > ret)
        ret = cb->g[m + BOTTOM]->liberties;
    return ret;
}

static void _eye_space_size_gt_two(
    const cfg_board * cb,
    move m,
    bool searched[BOARD_SIZ * BOARD_SIZ],
    u8 * count
){
    if(searched[m] || cb->p[m] != EMPTY)
        return;

    searched[m] = true;
    (*count)++;

    if(*count > 2)
        return;

    if(!border_left[m])
        _eye_space_size_gt_two(cb, m + LEFT, searched, count);
    if(!border_right[m])
        _eye_space_size_gt_two(cb, m + RIGHT, searched, count);
    if(!border_top[m])
        _eye_space_size_gt_two(cb, m + TOP, searched, count);
    if(!border_bottom[m])
        _eye_space_size_gt_two(cb, m + BOTTOM, searched, count);
}

/*
Count the size of the empty space in an empty area.
RETURNS true if larger than two intersections
*/
bool eye_space_size_gt_two(
    const cfg_board * cb,
    move m
){
    u8 count = 0;
    bool searched[BOARD_SIZ * BOARD_SIZ];
    memset(searched, false, BOARD_SIZ * BOARD_SIZ);
    _eye_space_size_gt_two(cb, m, searched, &count);
    return count > 2;
}

/*
Tests whether a neighbor group of stone type stone has two liberties.
RETURNS true if neighbor group is put in atari
*/
bool puts_neighbor_in_atari(
    const cfg_board * cb,
    move m,
    u8 stone
){
    if(!border_left[m] && cb->p[m + LEFT] == stone && cb->g[m + LEFT]->liberties
        == 2)
        return true;
    if(!border_right[m] && cb->p[m + RIGHT] == stone && cb->g[m +
        RIGHT]->liberties == 2)
        return true;
    if(!border_top[m] && cb->p[m + TOP] == stone && cb->g[m + TOP]->liberties ==
        2)
        return true;
    return (!border_bottom[m] && cb->p[m + BOTTOM] == stone && cb->g[m +
        BOTTOM]->liberties == 2);
}

/*
Return the maximum number of stones of a group of stones of value stone;
adjacent to the intersection m.
RETURNS maximum number of stones of a group, or 0
*/
u16 max_neighbor_group_stones(
    const cfg_board * cb,
    move m,
    u8 stone
){
    assert(is_board_move(m));

    u16 ret = 0;
    if(!border_left[m] && cb->p[m + LEFT] == stone)
        ret = cb->g[m + LEFT]->stones.count;
    if(!border_right[m] && cb->p[m + RIGHT] == stone && cb->g[m +
        RIGHT]->stones.count > ret)
        ret = cb->g[m + RIGHT]->stones.count;
    if(!border_top[m] && cb->p[m + TOP] == stone && cb->g[m +
        TOP]->stones.count > ret)
        ret = cb->g[m + TOP]->stones.count;
    if(!border_bottom[m] && cb->p[m + BOTTOM] == stone && cb->g[m +
        BOTTOM]->stones.count > ret)
        ret = cb->g[m + BOTTOM]->stones.count;
    return ret;
}

/*
Tests whether two groups have exactly the same liberties.
RETURNS true if the groups have the exact same liberties
*/
bool groups_same_liberties(
    const group * restrict g1,
    const group * restrict g2
){
    return memcmp(g1->ls, g2->ls, LIB_BITMAP_SIZ) == 0;
}

/*
Tests whether two groups share at least one liberty.
RETURNS true if the groups share at least one liberty
*/
bool groups_share_liberties(
    const group * restrict g1,
    const group * restrict g2
){
    for(u8 i = 0; i < LIB_BITMAP_SIZ; ++i)
        if((g1->ls[i] & g2->ls[i]) > 0)
            return true;
    return false;
}

/*
Counts the number of shared liberties between two groups.
RETURNS number of shared liberties
*/
u8 groups_shared_liberties(
    const group * restrict g1,
    const group * restrict g2
){
    u8 ret = 0;
    for(u8 i = 0; i < LIB_BITMAP_SIZ; ++i)
        ret += dyn_active_bits[g1->ls[i] & g2->ls[i]];
    return ret;
}



static bool can_be_killed2(
    cfg_board * b,
    move om,
    bool is_black,
    u32 depth
);

/*
Attempt attack on group with 1 or 2 liberties.
*/
static bool can_be_killed3(
    cfg_board * cb,
    move om,
    bool is_black,
    u32 depth
){
    if(depth >= (BOARD_SIZ * BOARD_SIZ) / 2)
        return false; /* probably superko */

    group * g = cb->g[om];

    if(g->liberties < 2)
        return true;

    if(g->liberties > 2)
        return false;

    move m = get_1st_liberty(g);
    if(can_play(cb, m, is_black))
    {
        cfg_board tmp;
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, m, is_black);
        if(can_be_killed2(&tmp, om, !is_black, depth + 1))
        {
            cfg_board_free(&tmp);
            return true;
        }
        cfg_board_free(&tmp);
    }

    m = get_next_liberty(g, m);
    if(can_play(cb, m, is_black))
    {
        just_play(cb, m, is_black);
        if(can_be_killed2(cb, om, !is_black, depth + 1))
            return true;
    }

    return false;
}

/*
Defend a group with 1 liberty, playing or passing.
*/
static bool can_be_killed2(
    cfg_board * cb,
    move om,
    bool is_black,
    u32 depth
){
    if(depth >= (BOARD_SIZ * BOARD_SIZ) / 2)
        return false; /* probably superko */

    group * g = cb->g[om];

    if(g->liberties > 2)
        return false;

    cfg_board tmp;

    /* try a capture if possible */
    for(u16 k = 0; k < g->neighbors_count; ++k)
    {
        group * n = cb->g[g->neighbors[k]];
        if(n->liberties == 1 && !groups_share_liberties(g, n))
        {
            move m = get_1st_liberty(n);
            if(can_play(cb, m, is_black))
            {
                cfg_board_clone(&tmp, cb);
                just_play(&tmp, m, is_black);
                if(!can_be_killed3(&tmp, om, !is_black, depth + 1))
                {
                    cfg_board_free(&tmp);
                    return false;
                }
                cfg_board_free(&tmp);
            }
        }
    }

    /* try 1st liberty */
    move m = get_1st_liberty(g);
    if(can_play(cb, m, is_black))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, m, is_black);
        if(!can_be_killed3(&tmp, om, !is_black, depth + 1))
        {
            cfg_board_free(&tmp);
            return false;
        }
        cfg_board_free(&tmp);
    }

    if(g->liberties == 2)
    {
        m = get_next_liberty(g, m);
        if(can_play(cb, m, is_black))
        {
            cfg_board_clone(&tmp, cb);
            just_play(&tmp, m, is_black);
            if(!can_be_killed3(&tmp, om, !is_black, depth + 1))
            {
                cfg_board_free(&tmp);
                return false;
            }
            cfg_board_free(&tmp);
        }
    }

    /* what about just passing/playing elsewhere? */
    just_pass(cb);

    return can_be_killed3(cb, om, !is_black, depth + 1);
}

/*
Tests whether group g can be attacked and eventually killed by its opponent,
with no chance of making at least three liberties.
RETURNS play that ensures the group is killed, or NONE
*/
move can_be_killed(
    const cfg_board * cb,
    const group * g
){
    assert(g->liberties > 0);

    if(g->liberties < 2)
    {
        move m = get_1st_liberty(g);
        return ko_violation(cb, m) ? NONE : m;
    }

    if(g->liberties > 3)
        return NONE;

    cfg_board tmp;

    /* attempt attack group */
    move m = get_1st_liberty(g);
    if(can_play(cb, m, !g->is_black))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, m, !g->is_black);
        if(can_be_killed2(&tmp, g->stones.coord[0], g->is_black, 0))
        {
            cfg_board_free(&tmp);
            return m;
        }
        cfg_board_free(&tmp);
    }

    m = get_next_liberty(g, m);
    if(can_play(cb, m, !g->is_black))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, m, !g->is_black);
        if(can_be_killed2(&tmp, g->stones.coord[0], g->is_black, 0))
        {
            cfg_board_free(&tmp);
            return m;
        }
        cfg_board_free(&tmp);
    }

    if(g->liberties == 3)
    {
        m = get_next_liberty(g, m);
        if(can_play(cb, m, !g->is_black))
        {
            cfg_board_clone(&tmp, cb);
            just_play(&tmp, m, !g->is_black);
            if(can_be_killed2(&tmp, g->stones.coord[0], g->is_black, 0))
            {
                cfg_board_free(&tmp);
                return m;
            }
            cfg_board_free(&tmp);
        }
    }

    return NONE;
}


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
){
    assert(g->liberties > 0);

    if(g->liberties < 2)
    {
        move m = get_1st_liberty(g);
        if(!ko_violation(cb, m))
        {
            plays[*plays_count] = m;
            (*plays_count)++;
        }
        return;
    }

    if(g->liberties > 3)
        return;

    cfg_board tmp;

    /* attempt attack group */
    move m = get_1st_liberty(g);
    if(can_play(cb, m, !g->is_black))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, m, !g->is_black);
        if(can_be_killed2(&tmp, g->stones.coord[0], g->is_black, 0))
        {
            plays[*plays_count] = m;
            (*plays_count)++;
        }
        cfg_board_free(&tmp);
    }

    m = get_next_liberty(g, m);
    if(can_play(cb, m, !g->is_black))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, m, !g->is_black);
        if(can_be_killed2(&tmp, g->stones.coord[0], g->is_black, 0))
        {
            plays[*plays_count] = m;
            (*plays_count)++;
        }
        cfg_board_free(&tmp);
    }

    if(g->liberties == 3)
    {
        m = get_next_liberty(g, m);
        if(can_play(cb, m, !g->is_black))
        {
            cfg_board_clone(&tmp, cb);
            just_play(&tmp, m, !g->is_black);
            if(can_be_killed2(&tmp, g->stones.coord[0], g->is_black, 0))
            {
                plays[*plays_count] = m;
                (*plays_count)++;
            }
            cfg_board_free(&tmp);
        }
    }
}


/*
Tests whether group g can be led to have at least three liberties regardless of
the opponent attack. None is also returned if the saving play is to pass.
RETURNS one play that saves the group from being killed, or NONE
*/
move can_be_saved(
    const cfg_board * cb,
    const group * g
){
    if(g->liberties > 3)
        return true;

    cfg_board tmp;

    /* attempt defend group */
    move m = get_1st_liberty(g);
    if(can_play(cb, m, g->is_black))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, m, g->is_black);
        if(!can_be_killed3(&tmp, g->stones.coord[0], !g->is_black, 0))
        {
            cfg_board_free(&tmp);
            return m;
        }
        cfg_board_free(&tmp);
    }

    if(g->liberties > 1)
    {
        m = get_next_liberty(g, m);
        if(can_play(cb, m, g->is_black))
        {
            cfg_board_clone(&tmp, cb);
            just_play(&tmp, m, g->is_black);
            if(!can_be_killed3(&tmp, g->stones.coord[0], !g->is_black, 0))
            {
                cfg_board_free(&tmp);
                return m;
            }
            cfg_board_free(&tmp);
        }

        if(g->liberties > 2)
        {
            m = get_next_liberty(g, m);
            if(can_play(cb, m, g->is_black))
            {
                cfg_board_clone(&tmp, cb);
                just_play(&tmp, m, g->is_black);
                if(!can_be_killed3(&tmp, g->stones.coord[0], !g->is_black, 0))
                {
                    cfg_board_free(&tmp);
                    return m;
                }
                cfg_board_free(&tmp);
            }
        }
    }

    for(u16 k = 0; k < g->neighbors_count; ++k)
    {
        group * n = cb->g[g->neighbors[k]];
        if(n->liberties == 1)
        {
            m = get_1st_liberty(n);
            if(can_play(cb, m, g->is_black))
                return m;
        }
    }

    return NONE;
}

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
){
    if(g->liberties > 3)
        return;

    cfg_board tmp;

    /* attempt defend group */
    move m = get_1st_liberty(g);
    if(can_play(cb, m, g->is_black))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, m, g->is_black);
        if(!can_be_killed3(&tmp, g->stones.coord[0], !g->is_black, 0))
        {
            plays[*plays_count] = m;
            (*plays_count)++;
        }
        cfg_board_free(&tmp);
    }

    if(g->liberties > 1)
    {
        m = get_next_liberty(g, m);
        if(can_play(cb, m, g->is_black))
        {
            cfg_board_clone(&tmp, cb);
            just_play(&tmp, m, g->is_black);
            if(!can_be_killed3(&tmp, g->stones.coord[0], !g->is_black, 0))
            {
                plays[*plays_count] = m;
                (*plays_count)++;
            }
            cfg_board_free(&tmp);
        }

        if(g->liberties > 2)
        {
            m = get_next_liberty(g, m);
            if(can_play(cb, m, g->is_black))
            {
                cfg_board_clone(&tmp, cb);
                just_play(&tmp, m, g->is_black);
                if(!can_be_killed3(&tmp, g->stones.coord[0], !g->is_black, 0))
                {
                    plays[*plays_count] = m;
                    (*plays_count)++;
                }
                cfg_board_free(&tmp);
            }
        }
    }

    for(u16 k = 0; k < g->neighbors_count; ++k)
    {
        group * n = cb->g[g->neighbors[k]];
        if(n->liberties == 1)
        {
            m = get_1st_liberty(n);
            if(can_play(cb, m, g->is_black))
            {
                plays[*plays_count] = m;
                (*plays_count)++;
            }
        }
    }
}

