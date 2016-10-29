/*
Tactical functions that make use of a cfg_board structure.

Tactical analysis in Matilda mostly concerns with two things:
- Eye shape -- eyes, nakade
- Life and death -- ladders, seki, 1-2 liberty solvers for killing and saving
groups, connecting groups by kosumi, bamboo joints, etc for the purpose of eye
counting.
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
#include "tactical.h"
#include "types.h"


extern u8 out_neighbors8[TOTAL_BOARD_SIZ];
extern u8 out_neighbors4[TOTAL_BOARD_SIZ];
extern move_seq neighbors_3x3[TOTAL_BOARD_SIZ];

extern bool border_left[TOTAL_BOARD_SIZ];
extern bool border_right[TOTAL_BOARD_SIZ];
extern bool border_top[TOTAL_BOARD_SIZ];
extern bool border_bottom[TOTAL_BOARD_SIZ];

/* number of active bits for every byte combination */
extern u8 active_bits_in_byte[256];

extern bool black_eye[65536];
extern bool white_eye[65536];

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
){
#if 1
    return is_black ? black_eye[cb->hash[m]] : white_eye[cb->hash[m]];
#else
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
#endif
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
    bool is_black,
    move m,
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
    bool is_black,
    move m,
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
    bool is_black,
    move m
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
    bool near_pos[TOTAL_BOARD_SIZ],
    const cfg_board * cb,
    move m
){
    memset(near_pos, false, TOTAL_BOARD_SIZ);
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
    group * g = cb->g[om];

    if(g->liberties < 2)
        return true;

    if(g->liberties > 2)
        return false;

    if(depth >= BOARD_SIZ * 3)
        return false; /* probably superko */

    move m = get_1st_liberty(g);
    if(can_play(cb, is_black, m))
    {
        cfg_board tmp;
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, is_black, m);
        if(can_be_killed2(&tmp, om, !is_black, depth + 1))
        {
            cfg_board_free(&tmp);
            return true;
        }
        cfg_board_free(&tmp);
    }

    m = get_next_liberty(g, m);
    if(can_play(cb, is_black, m))
    {
        just_play(cb, is_black, m);
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
    group * g = cb->g[om];

    if(g->liberties > 2)
        return false;

    if(depth >= BOARD_SIZ * 3)
        return false; /* probably superko */

    cfg_board tmp;

    /* try a capture if possible */
    for(u16 k = 0; k < g->neighbors_count; ++k)
    {
        group * n = cb->g[g->neighbors[k]];
        if(n->liberties == 1 && !groups_share_liberties(g, n))
        {
            move m = get_1st_liberty(n);
            if(can_play(cb, is_black, m))
            {
                cfg_board_clone(&tmp, cb);
                just_play(&tmp, is_black, m);
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
    if(can_play(cb, is_black, m))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, is_black, m);
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
        if(can_play(cb, is_black, m))
        {
            cfg_board_clone(&tmp, cb);
            just_play(&tmp, is_black, m);
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
move get_killing_play(
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
    if(can_play(cb, !g->is_black, m))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, !g->is_black, m);
        if(can_be_killed2(&tmp, g->stones.coord[0], g->is_black, 0))
        {
            cfg_board_free(&tmp);
            return m;
        }
        cfg_board_free(&tmp);
    }

    m = get_next_liberty(g, m);
    if(can_play(cb, !g->is_black, m))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, !g->is_black, m);
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
        if(can_play(cb, !g->is_black, m))
        {
            cfg_board_clone(&tmp, cb);
            just_play(&tmp, !g->is_black, m);
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
    if(can_play(cb, !g->is_black, m))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, !g->is_black, m);
        if(can_be_killed2(&tmp, g->stones.coord[0], g->is_black, 0))
        {
            plays[*plays_count] = m;
            (*plays_count)++;
        }
        cfg_board_free(&tmp);
    }

    m = get_next_liberty(g, m);
    if(can_play(cb, !g->is_black, m))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, !g->is_black, m);
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
        if(can_play(cb, !g->is_black, m))
        {
            cfg_board_clone(&tmp, cb);
            just_play(&tmp, !g->is_black, m);
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
Assuming the group is in danger, attempts to find a play that will produce at
least three liberties, regardless of opponent play.
RETURNS  play that saves the group from being killed, or NONE
*/
move get_saving_play(
    const cfg_board * cb,
    const group * g
){
    cfg_board tmp;

    /* try a capture if possible */
    for(u16 k = 0; k < g->neighbors_count; ++k)
    {
        group * n = cb->g[g->neighbors[k]];
        if(n->liberties == 1 && !groups_share_liberties(g, n))
        {
            move m = get_1st_liberty(n);
            if(can_play(cb, g->is_black, m))
            {
                cfg_board_clone(&tmp, cb);
                just_play(&tmp, g->is_black, m);
                if(!can_be_killed3(&tmp, g->stones.coord[0], !g->is_black, 0))
                {
                    cfg_board_free(&tmp);
                    return m;
                }
                cfg_board_free(&tmp);
            }
        }
    }

    /* attempt defend group */
    move m = get_1st_liberty(g);
    if(can_play(cb, g->is_black, m))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, g->is_black, m);
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
        if(can_play(cb, g->is_black, m))
        {
            cfg_board_clone(&tmp, cb);
            just_play(&tmp, g->is_black, m);
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
            if(can_play(cb, g->is_black, m))
            {
                cfg_board_clone(&tmp, cb);
                just_play(&tmp, g->is_black, m);
                if(!can_be_killed3(&tmp, g->stones.coord[0], !g->is_black, 0))
                {
                    cfg_board_free(&tmp);
                    return m;
                }
                cfg_board_free(&tmp);
            }
        }
    }

    return NONE;
}

/*
Tests whether group g can be led to have at least three liberties regardless of
opponent play.
RETURNS true if can be made to have at least three liberties
*/
bool can_be_saved(
    const cfg_board * cb,
    const group * g
){
    if(g->liberties > 3)
        return true;

    return get_saving_play(cb, g) != NONE;
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

    /* try a capture if possible */
    for(u16 k = 0; k < g->neighbors_count; ++k)
    {
        group * n = cb->g[g->neighbors[k]];
        if(n->liberties == 1 && !groups_share_liberties(g, n))
        {
            move m = get_1st_liberty(n);
            if(can_play(cb, g->is_black, m))
            {
                cfg_board_clone(&tmp, cb);
                just_play(&tmp, g->is_black, m);
                if(!can_be_killed3(&tmp, g->stones.coord[0], !g->is_black, 0))
                {
                    plays[*plays_count] = m;
                    (*plays_count)++;
                }
                cfg_board_free(&tmp);
            }
        }
    }

    /* attempt defend group */
    move m = get_1st_liberty(g);
    if(can_play(cb, g->is_black, m))
    {
        cfg_board_clone(&tmp, cb);
        just_play(&tmp, g->is_black, m);
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
        if(can_play(cb, g->is_black, m))
        {
            cfg_board_clone(&tmp, cb);
            just_play(&tmp, g->is_black, m);
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
            if(can_play(cb, g->is_black, m))
            {
                cfg_board_clone(&tmp, cb);
                just_play(&tmp, g->is_black, m);
                if(!can_be_killed3(&tmp, g->stones.coord[0], !g->is_black, 0))
                {
                    plays[*plays_count] = m;
                    (*plays_count)++;
                }
                cfg_board_free(&tmp);
            }
        }
    }
}

