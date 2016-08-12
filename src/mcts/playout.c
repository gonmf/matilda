/*
Heavy playout implementation with probability distribution selection and the use
of a play status cache.

The move selection policy uses the following restrictions:
    1. No illegal plays
    2. No playing in own proper eyes
    3. No plays ending in self-atari except if forming a single stone group
    (throw-in)
And chooses a play based on (by order of importance):
    1. Nakade
    2. Capture
    3. Avoid capture
    4. Handcrafted 3x3 patterns
    5. Random play
*/

#include "matilda.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "board.h"
#include "cfg_board.h"
#include "hash_table.h"
#include "pat3.h"
#include "playout.h"
#include "randg.h"
#include "scoring.h"
#include "state_changes.h"
#include "tactical.h"
#include "types.h"
#include "frisbee.h"

u16 pl_skip_saving = PL_SKIP_SAVING;
u16 pl_skip_nakade = PL_SKIP_NAKADE;
u16 pl_skip_pattern = PL_SKIP_PATTERN;
u16 pl_skip_capture = PL_SKIP_CAPTURE;

extern move_seq neighbors_3x3[TOTAL_BOARD_SIZ];

/*
For mercy Threshold
*/
extern d16 komi_offset;
extern d16 komi;

/*
Frisbee Go
*/
extern float frisbee_prob;

static void invalidate_cache_of_the_past(
    const cfg_board * cb,
    u8 c1[TOTAL_BOARD_SIZ],
    u8 c2[TOTAL_BOARD_SIZ]
){
    /*
    Positions previously illegal because of possible ko
    */
    if(is_board_move(cb->last_eaten))
        c1[cb->last_eaten] = c2[cb->last_eaten] = CACHE_PLAY_DIRTY;
}

/*
Dirty:
corners of 3x3 shape
liberties of group of last play
liberties of neighbor groups to last play
positions marked captured
*/
static void invalidate_cache_after_play(
    const cfg_board * cb,
    u8 c1[TOTAL_BOARD_SIZ],
    u8 c2[TOTAL_BOARD_SIZ],
    bool stones_captured[TOTAL_BOARD_SIZ],
    u8 libs_of_nei_of_captured[LIB_BITMAP_SIZ]
){
    assert(is_board_move(cb->last_played));

    move m = cb->last_played;
    /* Position just played at is certain to be illegal */
    c1[m] = c2[m] = 0;

    /*
    Invalidate corners

    Because of 3x3 neighborhood testing in heavy playouts
    */
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);
    if(x > 0)
    {
        if(y > 0)
            c1[m + LEFT + TOP] = c2[m + LEFT + TOP] = CACHE_PLAY_DIRTY;

        if(y < BOARD_SIZ - 1)
            c1[m + LEFT + BOTTOM] = c2[m + LEFT + BOTTOM] = CACHE_PLAY_DIRTY;

    }
    if(x < BOARD_SIZ - 1)
    {
        if(y > 0)
            c1[m + RIGHT + TOP] = c2[m + RIGHT + TOP] = CACHE_PLAY_DIRTY;

        if(y < BOARD_SIZ - 1)
            c1[m + RIGHT + BOTTOM] = c2[m + RIGHT + BOTTOM] = CACHE_PLAY_DIRTY;
    }

    /* Mix liberties of neighbors of eaten stones and new group */
    for(u8 i = 0; i < LIB_BITMAP_SIZ; ++i)
        libs_of_nei_of_captured[i] |= cb->g[m]->ls[i];

    /* Mix liberties of neighbors of new group */
    for(u8 n = 0; n < cb->g[m]->neighbors_count; ++n)
    {
        group * g = cb->g[cb->g[m]->neighbors[n]];
        for(u8 i = 0; i < LIB_BITMAP_SIZ; ++i)
            libs_of_nei_of_captured[i] |= g->ls[i];
    }


    /* Dirty liberties and positions eaten */
    for(m = 0; m < TOTAL_BOARD_SIZ; ++m)
    {
        if(stones_captured[m])
        {
            c1[m] = c2[m] = CACHE_PLAY_DIRTY;
            continue;
        }

        u8 mask = (1 << (m % 8));
        if(libs_of_nei_of_captured[m / 8] & mask)
            c1[m] = c2[m] = CACHE_PLAY_DIRTY;
    }
}

/*
Selects the next play of a heavy playout - MoGo style.
Uses a cache of play statuses that is updated as needed.
*/
move heavy_select_play(
    cfg_board * cb,
    bool is_black,
    u8 cache[TOTAL_BOARD_SIZ]
){
    u8 libs;
    bool captures;
    u8 opt = is_black ? WHITE_STONE : BLACK_STONE;

    for(u16 k = 0; k < cb->empty.count; ++k)
    {
        move m = cb->empty.coord[k];
        if(cache[m] & CACHE_PLAY_DIRTY)
        {
            if(!is_eye(cb, m, is_black) && !ko_violation(cb, m) && (libs =
                safe_to_play(cb, m, is_black, &captures)) > 0)
            {

                /*
                Prohibit self-ataris if they don't put the opponent in atari
                (this definition covers throw-ins)
                */
                if(libs == 1 && !captures && !puts_neighbor_in_atari(cb, m,
                    opt))
                {
                    cache[m] = 0;
                    continue;
                }

                if(is_relaxed_eye(cb, m, is_black))
                {
                    cache[m] = 0;
                    continue;
                }

                bool can_have_forcing_move;
                /*
                Avoid defending 2-point eye unnecessarily
                */
                if(is_2pt_eye(cb, m, is_black, &can_have_forcing_move))
                {
                    cache[m + RIGHT] = 0;
                    cache[m + BOTTOM] = 0;
                    if(!can_have_forcing_move)
                    {
                        cache[m] = 0;
                        continue;
                    }
                }
                else
                {
                    /*
                    Avoid defending 4-point eye unnecessarily
                    */
                    if(is_4pt_eye(cb, m, is_black, &can_have_forcing_move))
                    {
                        cache[m + RIGHT] = 0;
                        cache[m + BOTTOM] = 0;
                        cache[m + RIGHT + BOTTOM] = 0;
                        if(!can_have_forcing_move)
                        {
                            cache[m] = 0;
                            continue;
                        }
                    }
                    else
                    {
                        /*
                        Reduce area of attack of opponent 4-point eye
                        */
                        if(is_4pt_eye(cb, m, !is_black, &can_have_forcing_move))
                        {
                            cache[m + RIGHT] = 0;
                            cache[m + BOTTOM] = 0;
                            cache[m + RIGHT + BOTTOM] = 0;
                        }
                        else
                        {
                            /*
                            Reduce area of attack of opponent 2-point eye
                            */
                            if(is_2pt_eye(cb, m, !is_black,
                                &can_have_forcing_move))
                            {
                                cache[m + RIGHT] = 0;
                                cache[m + BOTTOM] = 0;
                            }
                        }
                    }
                }

                cache[m] = CACHE_PLAY_LEGAL;

                if(libs > 1)
                    cache[m] |= CACHE_PLAY_OSAFE;

                if(captures)
                    cache[m] |= CACHE_PLAY_CAPTS;

            }
            else
            {
                cache[m] = 0; /* not dirty and not legal either */
                continue;
            }
        }
    }

    u16 candidate_plays = 0;
    /* x2 because the same liberties can appear repeated when adding neighbor
    liberties */
    move candidate_play[TOTAL_BOARD_SIZ * 2];
    u16 weights[TOTAL_BOARD_SIZ * 2];
    u16 weight_total = 0;




    if(rand_u16(128) >= pl_skip_saving && is_board_move(cb->last_played))
    {
        /*
        Avoid being captured after last play
        */
        group * last_play_group = cb->g[cb->last_played];
        for(u16 k = 0; k < last_play_group->neighbors_count; ++k)
        {
            group * g = cb->g[last_play_group->neighbors[k]];
            if(g->liberties == 1)
            {
                /* Play at remaining liberty */
                move m = get_1st_liberty(g);
                if(cache[m] & CACHE_PLAY_OSAFE)
                {
                    u16 w = g->stones.count + 2;
                    weights[candidate_plays] = w;
                    candidate_play[candidate_plays] = m;
                    weight_total += w;
                    ++candidate_plays;
                }
                /* Kill opposing group to make liberties */
                for(u16 l = 0; l < g->neighbors_count; ++l)
                {
                    group * h = cb->g[g->neighbors[l]];
                    if(h->liberties == 1)
                    {
                        m = get_1st_liberty(h);
                        if(cache[m] & CACHE_PLAY_LEGAL)
                        {
                            u16 w = h->stones.count + 2;
                            if(cache[m] & CACHE_PLAY_OSAFE)
                                w *= 2;
                            weights[candidate_plays] = w;
                            candidate_play[candidate_plays] = m;
                            weight_total += w;
                            ++candidate_plays;
                        }
                    }
                }
            }
        }
        if(weight_total > 0)
        {
            d32 w = (d32)rand_u16(weight_total);
            for(u16 i = 0; ; ++i)
            {
                w -= weights[i];
                if(w < 0)
                    return candidate_play[i];
            }
        }
    }


    /*
    Nakade
    */
    if(rand_u16(128) >= pl_skip_nakade)
    {
        for(u16 k = 0; k < cb->empty.count; ++k)
        {
            move m = cb->empty.coord[k];
            if(cache[m] & CACHE_PLAY_OSAFE)
            {
                u16 w;
                if((w = is_nakade(cb, m)) > 0)
                {
                    weights[candidate_plays] = w;
                    candidate_play[candidate_plays] = m;
                    weight_total += w;
                    ++candidate_plays;
                }
            }
        }
        if(weight_total > 0)
        {
            d32 w = (d32)rand_u16(weight_total);
            for(u16 i = 0; ; ++i)
            {
                w -= weights[i];
                if(w < 0)
                    return candidate_play[i];
            }
        }
    }

    /*
    Play a capturing move
    */
    if(rand_u16(128) >= pl_skip_capture)
    {
        for(u16 k = 0; k < cb->empty.count; ++k)
        {
            move m = cb->empty.coord[k];
            if(cache[m] & CACHE_PLAY_CAPTS)
            {
                candidate_play[candidate_plays] = m;
                ++candidate_plays;
            }
        }
        if(candidate_plays > 0)
        {
            u16 p = rand_u16(candidate_plays);
            return candidate_play[p];
        }
    }


    if(rand_u16(128) >= pl_skip_pattern && is_board_move(cb->last_played))
    {
        /*
        Match 3x3 patterns in 8 neighbor intersections
        */
        for(move k = 0; k < neighbors_3x3[cb->last_played].count; ++k)
        {
            move m = neighbors_3x3[cb->last_played].coord[k];
            if(cache[m] & CACHE_PLAY_OSAFE)
            {
                u16 w = pat3_find(cb->hash[m], is_black);
                if(w != 0)
                {
                    weights[candidate_plays] = w;
                    candidate_play[candidate_plays] = m;
                    weight_total += w;
                    ++candidate_plays;
                }
            }
        }
        if(weight_total > 0)
        {
            d32 w = (d32)rand_u16(weight_total);
            for(u16 i = 0; ; ++i)
            {
                w -= weights[i];
                if(w < 0)
                    return candidate_play[i];
            }
        }
    }

    /*
    Play random legal play
    */
    for(u16 k = 0; k < cb->empty.count; ++k)
    {
        move m = cb->empty.coord[k];
        if(cache[m] & CACHE_PLAY_LEGAL)
        {
            u16 w = (cache[m] & CACHE_PLAY_OSAFE)? 2 : 1;
            weights[candidate_plays] = w;
            candidate_play[candidate_plays] = m;
            weight_total += w;
            ++candidate_plays;
        }
    }
    if(weight_total > 0)
    {
        d32 w = (d32)rand_u16(weight_total);
        for(u16 i = 0; ; ++i)
        {
            w -= weights[i];
            if(w < 0)
                return candidate_play[i];
        }
    }

    /*
        Pass
    */
    return PASS;
}


/*
Make a heavy playout and returns whether black wins.
Does not play in own proper eyes or self-ataris except for possible throw-ins.
Avoids too many ko battles. Also uses mercy threshold.
Also updates AMAF transitions information.
RETURNS the final score
*/
d16 playout_heavy_amaf(
    cfg_board * cb,
    bool is_black,
    u8 traversed[TOTAL_BOARD_SIZ]
){
    assert(verify_cfg_board(cb));
    u16 depth_max = MAX_PLAYOUT_DEPTH_OVER_EMPTY + cb->empty.count +
    rand_u16(2);
    /* stones are counted as 2 units in matilda */
    d16 diff = stone_diff(cb->p) - (komi_offset + komi) / 2;

    u8 b_cache[TOTAL_BOARD_SIZ];
    u8 w_cache[TOTAL_BOARD_SIZ];
    memset(b_cache, CACHE_PLAY_DIRTY, TOTAL_BOARD_SIZ);
    memset(w_cache, CACHE_PLAY_DIRTY, TOTAL_BOARD_SIZ);
    bool stones_captured[TOTAL_BOARD_SIZ];
    u8 libs_of_nei_of_captured[LIB_BITMAP_SIZ];

    while(--depth_max)
    {
        move m = heavy_select_play(cb, is_black, is_black ? b_cache : w_cache);
        assert(verify_cfg_board(cb));

        if(m == PASS) /* only passes when there are no more plays */
        {
            if(cb->last_played == PASS)
                break;
            invalidate_cache_of_the_past(cb, b_cache, w_cache);
            just_pass(cb);
            assert(verify_cfg_board(cb));
        }
        else
        {
            assert(is_board_move(m));

            invalidate_cache_of_the_past(cb, b_cache, w_cache);

#if ENABLE_FRISBEE_GO
            if(frisbee_prob < 1.0 && rand_float(1.0) > frisbee_prob)
            {
                m = random_shift_play(m);
                if(m == NONE || (is_black && ((b_cache[m] & CACHE_PLAY_LEGAL) ==
                    0)) || (!is_black && ((w_cache[m] & CACHE_PLAY_LEGAL) ==
                    0)))
                {
                    cb->last_played = cb->last_eaten = NONE;
                    is_black = !is_black;
                    continue;
                }
            }
#endif

            memset(stones_captured, 0, TOTAL_BOARD_SIZ);
            memset(libs_of_nei_of_captured, 0, LIB_BITMAP_SIZ);

            just_play3(cb, m, is_black, &diff, stones_captured,
                libs_of_nei_of_captured);

            assert(verify_cfg_board(cb));
            if(traversed[m] == EMPTY)
                traversed[m] = is_black ? BLACK_STONE : WHITE_STONE;
            if(abs(diff) > MERCY_THRESHOLD)
                return diff;
            invalidate_cache_after_play(cb, b_cache, w_cache, stones_captured,
                libs_of_nei_of_captured);
            assert(verify_cfg_board(cb));
        }

        is_black = !is_black;
    }

    return score_stones_and_area(cb->p);
}

/*
Strategy that uses the default policy of MCTS only
*/
void playout_as_strategy(
    const board * b,
    out_board * out_b
){
    pat3_init();
    cfg_board cb;
    cfg_from_board(&cb, b);

    u8 ignored_cache[TOTAL_BOARD_SIZ];
    memset(ignored_cache, CACHE_PLAY_DIRTY, TOTAL_BOARD_SIZ);

    /* only passes when there are no more plays */
    move m = heavy_select_play(&cb, true, ignored_cache);

    clear_out_board(out_b);
    if(m == PASS)
        out_b->pass = 1.0;
    else{
        out_b->value[m] = 1.0;
        out_b->tested[m] = true;
    }
}

