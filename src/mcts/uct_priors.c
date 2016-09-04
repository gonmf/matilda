/*
UCT expanded states initialization.
*/

#include "matilda.h"

#include <string.h>
#include <math.h> /* powf */

#include "board.h"
#include "cfg_board.h"
#include "dragon.h"
#include "move.h"
#include "pat3.h"
#include "priors.h"
#include "tactical.h"
#include "transpositions.h"
#include "types.h"

/*
Public to allow parameter optimization
*/
double prior_stone_scale_factor = PRIOR_STONE_SCALE_FACTOR;
u16 prior_even = PRIOR_EVEN;
u16 prior_nakade = PRIOR_NAKADE;
u16 prior_self_atari = PRIOR_SELF_ATARI;
u16 prior_attack = PRIOR_ATTACK;
u16 prior_defend = PRIOR_DEFEND;
u16 prior_pat3 = PRIOR_PAT3;
u16 prior_near_last = PRIOR_NEAR_LAST;
u16 prior_line2 = PRIOR_LINE2;
u16 prior_line3 = PRIOR_LINE3;
u16 prior_empty = PRIOR_EMPTY;
u16 prior_line1x = PRIOR_LINE1X;
u16 prior_line2x = PRIOR_LINE2X;
u16 prior_line3x = PRIOR_LINE3X;
u16 prior_corner = PRIOR_CORNER;
u16 prior_bad_play = PRIOR_BAD_PLAY;
u16 prior_pass = PRIOR_PASS;

extern u8 distances_to_border[TOTAL_BOARD_SIZ];
extern move_seq nei_dst_3[TOTAL_BOARD_SIZ];
extern u8 out_neighbors4[TOTAL_BOARD_SIZ];

extern bool border_left[TOTAL_BOARD_SIZ];
extern bool border_right[TOTAL_BOARD_SIZ];
extern bool border_top[TOTAL_BOARD_SIZ];
extern bool border_bottom[TOTAL_BOARD_SIZ];


static u16 stones_in_manhattan_dst3(
    const cfg_board * cb,
    move m
){
    u16 ret = 0;
    for(u16 n = 0; n < nei_dst_3[m].count; ++n)
    {
        move b = nei_dst_3[m].coord[n];
        if(cb->p[b] != EMPTY)
            ++ret;
    }
    return ret;
}

static void stats_add_play(
    tt_stats * stats,
    move m,
    u32 mc_w, /* wins */
    u32 mc_v /* visits */
){
    double mc_q = ((double)mc_w) / ((double)mc_v);
    u32 idx = stats->plays_count++;
    stats->plays[idx].m = m;
    stats->plays[idx].mc_q = mc_q;
    stats->plays[idx].mc_n = mc_v;


    /*
    Heuristic-MC

    Copying the MC prior values to AMAF and initializing other fields
    */
    stats->plays[idx].next_stats = NULL;
    /* AMAF/RAVE */
    stats->plays[idx].amaf_n = mc_v;
    stats->plays[idx].amaf_q = mc_q;

    /* LGRF */
    stats->plays[idx].lgrf1_reply = NULL;

    /* Criticality */
    stats->plays[idx].owner_winning = 0.5;
    stats->plays[idx].color_owning = 0.5;
}

/*
Priors values with heuristic MC-RAVE

Initiates the MCTS and AMAF statistics with the values from an external
heuristic.
Also marks playable positions, excluding playing in own eyes and ko violations,
with at least one visit.
*/
void init_new_state(
    cfg_board * cb,
    tt_stats * stats,
    bool is_black,
    const bool branch_limit[TOTAL_BOARD_SIZ]
){
    bool near_last_play[TOTAL_BOARD_SIZ];
    if(is_board_move(cb->last_played))
        mark_near_pos(near_last_play, cb, cb->last_played);
    else
        memset(near_last_play, false, TOTAL_BOARD_SIZ);

    u8 in_nakade[TOTAL_BOARD_SIZ];
    memset(in_nakade, 0, TOTAL_BOARD_SIZ);

    bool viable[TOTAL_BOARD_SIZ];
    memcpy(viable, branch_limit, TOTAL_BOARD_SIZ * sizeof(bool));
    bool play_okay[TOTAL_BOARD_SIZ];
    memcpy(play_okay, branch_limit, TOTAL_BOARD_SIZ * sizeof(bool));

    estimate_eyes(cb, is_black, viable, play_okay, in_nakade);

    u16 saving_play[TOTAL_BOARD_SIZ];
    memset(saving_play, 0, TOTAL_BOARD_SIZ * sizeof(u16));

    u16 capturable[TOTAL_BOARD_SIZ];
    memset(capturable, 0, TOTAL_BOARD_SIZ * sizeof(u16));

    bool self_atari[TOTAL_BOARD_SIZ];
    memset(self_atari, false, TOTAL_BOARD_SIZ);

    /*
    Tactical analysis of attack/defense of unsettled groups.
    */
    for(u8 i = 0; i < cb->unique_groups_count; ++i)
    {
        group * g = cb->g[cb->unique_groups[i]];
        if(g->eyes < 2)
        {
            move candidates[MAX_GROUPS];
            u16 candidates_count = 0;

            if(g->is_black == is_black)
            {
                move candidates2[MAX_GROUPS];
                u16 candidates_count2 = 0;
                can_be_killed_all(cb, g, &candidates_count2, candidates2);
                if(candidates_count2 > 0)
                {
                    can_be_saved_all(cb, g, &candidates_count, candidates);
                    if(candidates_count == 0)
                    {
                        for(u16 j = 0; j < candidates_count2; ++j)
                            self_atari[candidates2[j]] = true;
                        continue;
                    }
                    for(u16 j = 0; j < candidates_count; ++j)
                        saving_play[candidates[j]] += g->stones.count +
                    g->liberties;
                }
            }
            else
            {
                can_be_killed_all(cb, g, &candidates_count, candidates);
                if(candidates_count > 0)
                {
                    if(can_be_saved(cb, g) != NONE)
                        for(u16 j = 0; j < candidates_count; ++j)
                            capturable[candidates[j]] += g->stones.count +
                                g->liberties;
                    else
                        for(u16 j = 0; j < candidates_count; ++j)
                            capturable[candidates[j]] += (g->stones.count +
                                g->liberties) / 2;
                }
            }
        }
    }



    stats->plays_count = 0;

    for(move k = 0; k < cb->empty.count; ++k)
    {
        move m = cb->empty.coord[k];

        /*
        Don't play intersections disqualified because of a better, nearby nakade
        or because they are eyes
        */
        if(!viable[m])
            continue;

        bool captures;
        u8 libs = safe_to_play(cb, m, is_black, &captures);

        /*
        Don't play suicides
        */
        if(libs == 0)
            continue;

        /*
        Ko violation
        */
        if(captures && ko_violation(cb, m))
            continue;

        /*
        Even game heuristic
        */
        u32 mc_w = prior_even / 2;
        u32 mc_v = prior_even;

        /*
        Avoid typically poor plays like eye shape
        */
        if(!play_okay[m])
            mc_v += prior_bad_play;
        else
        {
            /*
            Avoid safe tiger mouths.
            */
            if(safe_tigers_mouth(cb, is_black, m))
                mc_v += prior_bad_play;
        }

        if(out_neighbors4[m] == 2 && ((is_black && cb->white_neighbors8[m] == 0)
            || (!is_black && cb->black_neighbors8[m] == 0)))
            mc_v += prior_bad_play;

        /*
        Prohibit self-ataris if they don't put the opponent in atari
        (this definition does not prohibit throw-ins)
        */
        // TODO this self atari is wrong; test replacing this test
        if(libs == 1 || self_atari[m])
            mc_v += prior_self_atari;

        /*
        Nakade
        */
        if(in_nakade[m] > 0)
        {
            group * g = get_closest_group(cb, m);
            if(g->eyes < 2) /* nakade eye shape is already an eye */
            {
                u16 b = (u16)powf(in_nakade[m], prior_stone_scale_factor);
                mc_w += prior_nakade + b;
                mc_v += prior_nakade + b;
            }
        }

        /*
        Saving plays
        */
        if(saving_play[m] > 0)
        {
            u16 b = (u16)powf(saving_play[m], prior_stone_scale_factor);
            mc_w += prior_defend + b;
            mc_v += prior_defend + b;
        }

        /*
        Capturing plays
        */
        if(capturable[m] > 0)
        {
            u16 b = (u16)powf(capturable[m], prior_stone_scale_factor);
            mc_w += prior_attack + b;
            mc_v += prior_attack + b;
        }

        /*
        3x3 patterns
        */
        if(libs > 1 && pat3_find(cb->hash[m], is_black) != 0)
        {
            mc_w += prior_pat3;
            mc_v += prior_pat3;
        }


        /*
        Favor plays near to the last and its group liberties
        */
        if(near_last_play[m])
        {
            mc_w += prior_near_last;
            mc_v += prior_near_last;
        }


        /*
        Bonuses based on line and empty parts of the board
        */
        if(stones_in_manhattan_dst3(cb, m) == 0)
        {
            u8 dst_border = distances_to_border[m];
            switch(dst_border)
            {
                case 0:
                    continue;
                case 1:
                    mc_v += prior_line2;
                    break;
                case 2:
                    mc_w += prior_line3;
                    mc_v += prior_line3;
                    break;
                default:
                    mc_w += prior_empty;
                    mc_v += prior_empty;
            }
        }
        else
        {
            u8 dst_border = distances_to_border[m];
            switch(dst_border)
            {
                case 0:
                    mc_v += prior_line1x;
                    break;
                case 1:
                    mc_v += prior_line2x;
                    break;
                case 2:
                    mc_w += prior_line3x;
                    mc_v += prior_line3x;
                    break;
            }
        }


        /*
        Corner of the board bonus
        */
        if(out_neighbors4[m] == 2)
        {
            mc_v += prior_corner;
        }


        stats_add_play(stats, m, mc_w, mc_v);
    }

    if(stats->plays_count < TOTAL_BOARD_SIZ / 8)
    {
        /*
        Add pass simulation
        */
        stats_add_play(stats, PASS, UCT_RESIGN_WINRATE * prior_pass,
            prior_pass);
    }
}

