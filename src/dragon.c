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

#include "config.h"

#include <assert.h>

#include "cfg_board.h"
#include "tactical.h"
#include "types.h"

extern move_seq neighbors_3x3[TOTAL_BOARD_SIZ];

extern bool border_left[TOTAL_BOARD_SIZ];
extern bool border_right[TOTAL_BOARD_SIZ];
extern bool border_top[TOTAL_BOARD_SIZ];
extern bool border_bottom[TOTAL_BOARD_SIZ];

static group * dragon_head(
    group * g
){
    while(g->next != NULL)
        g = g->next;
    return g;
}

static void unite_dragons(
    group * restrict g1,
    group * restrict g2
){
    assert(g1 != NULL);
    assert(g2 != NULL);
    group * g1h = dragon_head(g1);
    group * g2h = dragon_head(g2);
    if(g1h == g2h)
        return;

    if(g1h < g2h)
    {
        g1h->next = g2h;
        g2h->eyes += g1h->eyes;
        return;
    }

    g2h->next = g1h;
    g1h->eyes += g2h->eyes;
}

static void disqualify_square(
    bool viable[static TOTAL_BOARD_SIZ],
    move m
){
    /* this does not disqualify the center point */
    for(u8 k = 0; k < neighbors_3x3[m].count; ++k)
    {
        move n = neighbors_3x3[m].coord[k];
        viable[n] = false;
    }
}

/*
Produce counts of eyes for every group in the board, plus updates the viability
of playing at each position and whether such plays are nakade, from the
perspective of the current player.
*/
void estimate_eyes(
    cfg_board * cb,
    bool is_black,
    bool viable[static TOTAL_BOARD_SIZ],
    bool play_okay[static TOTAL_BOARD_SIZ],
    u8 in_nakade[static TOTAL_BOARD_SIZ]
){
    for(u8 i = 0; i < cb->unique_groups_count; ++i)
    {
        group * g = cb->g[cb->unique_groups[i]];
        g->eyes = 0;
        g->borrowed_eyes = 0;
        g->next = NULL;
    }

    for(move k = 0; k < cb->empty.count; ++k)
    {
        move m = cb->empty.coord[k];
        bool can_have_forcing_move;

        if(!viable[m] || !play_okay[m])
            continue;

        /*
        Eye shapes
        */
        if(is_eye(cb, is_black, m))
        {
            group * g = border_left[m] ? cb->g[m + RIGHT] : cb->g[m + LEFT];
            dragon_head(g)->eyes++;

            viable[m] = false;
            continue;
        }

        if(is_eye(cb, !is_black, m))
        {
            group * g = border_left[m] ? cb->g[m + RIGHT] : cb->g[m + LEFT];
            dragon_head(g)->eyes++;
            continue;
        }

        /*
        2-point eye shapes
        */
        if(is_2pt_eye(cb, is_black, m, &can_have_forcing_move))
        {
            group * g = get_closest_group(cb, m);
            dragon_head(g)->eyes++;

            play_okay[m + RIGHT] = false;
            play_okay[m + BOTTOM] = false;
            if(!can_have_forcing_move)
                play_okay[m] = false;
            continue;
        }

        if(is_2pt_eye(cb, !is_black, m, &can_have_forcing_move))
        {
            group * g = get_closest_group(cb, m);
            dragon_head(g)->eyes++;

            play_okay[m + RIGHT] = false;
            play_okay[m + BOTTOM] = false;
            continue;
        }

        /*
        Don't play in own and opponent big fours
        One of the plays is allowed to be used as a forcing move.
        */
        if(is_4pt_eye(cb, is_black, m, &can_have_forcing_move))
        {
            group * gs[4];
            u8 gsc = 0;
            if(!border_left[m])
                gs[gsc++] = cb->g[m + LEFT];
            if(!border_right[m + RIGHT])
                gs[gsc++] = cb->g[m + RIGHT + RIGHT];
            if(!border_top[m])
                gs[gsc++] = cb->g[m + TOP];
            if(!border_bottom[m + BOTTOM])
                gs[gsc++] = cb->g[m + BOTTOM + BOTTOM];

            for(u8 i = 1; i < gsc; ++i)
                unite_dragons(gs[0], gs[i]);
            dragon_head(gs[0])->eyes++;

            if(m == 0)
            {
                play_okay[m] = false;
                play_okay[m + RIGHT] = false;
                play_okay[m + BOTTOM] = false;
                if(!can_have_forcing_move)
                    play_okay[m + RIGHT + BOTTOM] = false;
            }
            else
            {
                play_okay[m + RIGHT] = false;
                play_okay[m + BOTTOM] = false;
                play_okay[m + RIGHT + BOTTOM] = false;
                if(!can_have_forcing_move)
                    play_okay[m] = false;
            }
            continue;
        }
        if(is_4pt_eye(cb, !is_black, m, &can_have_forcing_move))
        {
            group * gs[4];
            u8 gsc = 0;
            if(!border_left[m])
                gs[gsc++] = cb->g[m + LEFT];
            if(!border_right[m + RIGHT])
                gs[gsc++] = cb->g[m + RIGHT + RIGHT];
            if(!border_top[m])
                gs[gsc++] = cb->g[m + TOP];
            if(!border_bottom[m + BOTTOM])
                gs[gsc++] = cb->g[m + BOTTOM + BOTTOM];

            for(u8 i = 1; i < gsc; ++i)
                unite_dragons(gs[0], gs[i]);
            dragon_head(gs[0])->eyes++;

            if(m == 0)
            {
                play_okay[m] = false;
                play_okay[m + RIGHT] = false;
                play_okay[m + BOTTOM] = false;
            }
            else
            {
                play_okay[m + RIGHT] = false;
                play_okay[m + BOTTOM] = false;
                play_okay[m + RIGHT + BOTTOM] = false;
            }
            continue;
        }

        /*
        Nakade
        */
        u8 nk;
        if((nk = is_nakade(cb, m)) > 0)
        {
            group * gs[4];
            u8 gsc = 0;
            if(!border_left[m])
            {
                if(!border_top[m] && cb->g[m + LEFT + TOP] != NULL)
                    gs[gsc++] = cb->g[m + LEFT + TOP];
                if(!border_bottom[m] && cb->g[m + LEFT + BOTTOM] != NULL)
                    gs[gsc++] = cb->g[m + LEFT + BOTTOM];
            }
            if(!border_right[m])
            {
                if(!border_top[m] && cb->g[m + RIGHT + TOP] != NULL)
                    gs[gsc++] = cb->g[m + RIGHT + TOP];
                if(!border_bottom[m] && cb->g[m + RIGHT + BOTTOM] != NULL)
                    gs[gsc++] = cb->g[m + RIGHT + BOTTOM];
            }

            for(u8 i = 1; i < gsc; ++i)
                unite_dragons(gs[0], gs[i]);
            dragon_head(gs[0])->eyes++;

            disqualify_square(play_okay, m);
            in_nakade[m] = nk;
            continue;
        }

        if(is_corner_liberty(cb, true, m) || is_corner_liberty(cb, false, m))
            play_okay[m] = false;

        /*
        Bamboo joints
        */
        if(is_vertical_bamboo_joint(cb, m))
        {
            group * g1 = cb->g[m + TOP];
            group * g2 = cb->g[m + BOTTOM];
            if(g1 != g2 && groups_shared_liberties(g1, g2) == 2)
                unite_dragons(g1, g2);
        }
        else
            if(is_horizontal_bamboo_joint(cb, m))
            {
                group * g1 = cb->g[m + LEFT];
                group * g2 = cb->g[m + RIGHT];
                if(g1 != g2 && groups_shared_liberties(g1, g2) == 2)
                    unite_dragons(g1, g2);
            }
    }

    /*
    Add strong connections
    */
    for(u8 i = 0; i < cb->unique_groups_count; ++i)
    {
        group * g1 = cb->g[cb->unique_groups[i]];
        if(g1->liberties < 3)
            continue;
        for(u8 j = i + 1; j < cb->unique_groups_count; ++j)
        {
            group * g2 = cb->g[cb->unique_groups[j]];
            if(g2->liberties < 3 || g1->is_black != g2->is_black)
                continue;

            u8 shared = groups_shared_liberties(g1, g2);
            if(shared > 2)
            {
                unite_dragons(g1, g2);
                continue;
            }
        }
    }

    /*
    Add safe connections between groups with at least
    one independent liberty.
    */
    for(move k = 0; k < cb->empty.count; ++k)
    {
        move m = cb->empty.coord[k];

        if(!viable[m] || !play_okay[m])
            continue;

        if(sheltered_liberty(cb, m) && !is_eye(cb, !is_black, m))
        {
            group * gs[4];
            u8 fn = 0;
            if(!border_left[m])
            {
                gs[fn] = dragon_head(cb->g[m + LEFT]);
                if(gs[fn]->eyes == 0)
                    continue;
                ++fn;
            }
            if(!border_right[m])
            {
                gs[fn] = dragon_head(cb->g[m + RIGHT]);
                if(gs[fn]->eyes == 0)
                    continue;
                ++fn;
            }
            if(!border_top[m])
            {
                gs[fn] = dragon_head(cb->g[m + TOP]);
                if(gs[fn]->eyes == 0)
                    continue;
                ++fn;
            }
            if(!border_bottom[m])
            {
                gs[fn] = dragon_head(cb->g[m + BOTTOM]);
                if(gs[fn]->eyes == 0)
                    continue;
                ++fn;
            }
            /*
            Perfectly safe. We don't even need to
            borrow eyes because we are guaranteed
            to have two already.
            */
            for(u8 i = 1; i < fn; ++i)
                unite_dragons(gs[0], gs[i]);
            gs[0]->eyes++;
            play_okay[m] = false;
            continue;
        }
    }

    /*
    Finally add non-transitive eyes.
    */
    for(move k = 0; k < cb->empty.count; ++k)
    {
        move m = cb->empty.coord[k];
        /*
        Kosumi
        */
        if(is_kosumi1(cb, m))
        {
            group * g1 = dragon_head(cb->g[m + RIGHT]);
            group * g2 = dragon_head(cb->g[m + BOTTOM]);
            if(g1 != g2)
            {
                if(g2->eyes > g1->borrowed_eyes)
                    g1->borrowed_eyes = g2->eyes;
                if(g1->eyes > g2->borrowed_eyes)
                    g2->borrowed_eyes = g1->eyes;
            }
        }
        if(is_kosumi2(cb, m))
        {
            group * g1 = dragon_head(cb->g[m + LEFT]);
            group * g2 = dragon_head(cb->g[m + BOTTOM]);
            if(g1 != g2)
            {
                if(g2->eyes > g1->borrowed_eyes)
                    g1->borrowed_eyes = g2->eyes;
                if(g1->eyes > g2->borrowed_eyes)
                    g2->borrowed_eyes = g1->eyes;
            }
        }
    }

    /*
    Finally update counts irrespective of dragons
    */
    for(u8 i = 0; i < cb->unique_groups_count; ++i)
    {
        group * g = cb->g[cb->unique_groups[i]];
        group * dragon = dragon_head(g);
        if(dragon->borrowed_eyes > 0)
        {
            dragon->eyes += dragon->borrowed_eyes;
            dragon->borrowed_eyes = 0;
        }
        g->eyes = dragon->eyes;
    }
}
