/*
UCT expanded states initialization.
*/

#include "config.h"

#include <string.h>
#include <stdlib.h> /* qsort */
#include <math.h> /* powf */

#include "board.h"
#include "cfg_board.h"
#include "dragon.h"
#include "move.h"
#include "pat3.h"
#include "priors.h"
#include "pts_file.h"
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
u16 prior_corner = PRIOR_CORNER;
u16 prior_bad_play = PRIOR_BAD_PLAY;
u16 prior_pass = PRIOR_PASS;
u16 prior_starting_point = PRIOR_STARTING;


extern u8 distances_to_border[TOTAL_BOARD_SIZ];
extern move_seq nei_dst_3[TOTAL_BOARD_SIZ];
extern u8 out_neighbors4[TOTAL_BOARD_SIZ];

extern bool border_left[TOTAL_BOARD_SIZ];
extern bool border_right[TOTAL_BOARD_SIZ];
extern bool border_top[TOTAL_BOARD_SIZ];
extern bool border_bottom[TOTAL_BOARD_SIZ];

extern bool is_starting[TOTAL_BOARD_SIZ];


typedef struct __quality_pair_ {
    double quality;
    move play;
} quality_pair;



static u16 stones_in_manhattan_dst3(
    const cfg_board * cb,
    move m
) {
    u16 ret = 0;
    for (u16 n = 0; n < nei_dst_3[m].count; ++n) {
        move b = nei_dst_3[m].coord[n];
        if (cb->p[b] != EMPTY) {
            ++ret;
        }
    }
    return ret;
}

static void stats_add_play_tmp(
    tt_stats * stats,
    move m,
    u32 mc_w, /* wins */
    u32 mc_v /* visits */
) {
    u32 idx = stats->plays_count++;
    stats->plays[idx].m = m;
    stats->plays[idx].mc_q = mc_w;
    stats->plays[idx].mc_n = mc_v;

    stats->plays[idx].next_stats = NULL;

    /* LGRF */
    stats->plays[idx].lgrf1_reply = NULL;

    /* Criticality */
    stats->plays[idx].owner_winning = 0.5;
    stats->plays[idx].color_owning = 0.5;
}

/*
Heuristic-MC

Copying the MC prior values to AMAF and initializing other fields
*/
static void stats_add_play_final(
    tt_stats * stats,
    move m,
    double mc_q, /* quality */
    u32 mc_v /* visits */
) {
    u32 idx = stats->plays_count++;
    stats->plays[idx].m = m;
    stats->plays[idx].amaf_q = stats->plays[idx].mc_q = mc_q;
    stats->plays[idx].amaf_n = stats->plays[idx].mc_n = mc_v;

    stats->plays[idx].next_stats = NULL;

    /* LGRF */
    stats->plays[idx].lgrf1_reply = NULL;

    /* Criticality */
    stats->plays[idx].owner_winning = 0.5;
    stats->plays[idx].color_owning = 0.5;
}

static bool lib2_self_atari(
    cfg_board * cb,
    bool is_black,
    move m
) {
    cfg_board tmp;
    cfg_board_clone(&tmp, cb);
    just_play(&tmp, is_black, m);

    bool ret = is_board_move(get_killing_play(&tmp, tmp.g[m]));
    cfg_board_free(&tmp);

    return ret;
}

/*
Priors values with heuristic MC-RAVE

Initiates the MCTS and AMAF statistics with the values from an external
heuristic.
Also marks playable positions, excluding playing in own eyes and ko violations,
with at least one visit.
*/
void init_new_state(
    tt_stats * stats,
    cfg_board * cb,
    bool is_black
) {
    bool near_last_play[TOTAL_BOARD_SIZ];
    if (is_board_move(cb->last_played)) {
        mark_near_pos(near_last_play, cb, cb->last_played);
    } else {
        memset(near_last_play, false, TOTAL_BOARD_SIZ);
    }

    u8 in_nakade[TOTAL_BOARD_SIZ];
    memset(in_nakade, 0, TOTAL_BOARD_SIZ);

    bool viable[TOTAL_BOARD_SIZ];
    memset(viable, true, TOTAL_BOARD_SIZ);

    bool play_okay[TOTAL_BOARD_SIZ];
    memset(play_okay, true, TOTAL_BOARD_SIZ);

    estimate_eyes(cb, is_black, viable, play_okay, in_nakade);

    u16 saving_play[TOTAL_BOARD_SIZ];
    memset(saving_play, 0, TOTAL_BOARD_SIZ * sizeof(u16));

    u16 capturable[TOTAL_BOARD_SIZ];
    memset(capturable, 0, TOTAL_BOARD_SIZ * sizeof(u16));

    /*
    Tactical analysis of attack/defense of unsettled groups.
    */
    for (u8 i = 0; i < cb->unique_groups_count; ++i) {
        group * g = cb->g[cb->unique_groups[i]];
        if (g->eyes < 2) {
            move candidates[MAX_GROUPS];
            u16 candidates_count = 0;

            if (g->is_black == is_black) {
                if (get_killing_play(cb, g) != NONE) {
                    can_be_saved_all(cb, g, &candidates_count, candidates);

                    for (u16 j = 0; j < candidates_count; ++j) {
                        saving_play[candidates[j]] += g->stones.count + g->liberties;
                    }
                }
            } else {
                can_be_killed_all(cb, g, &candidates_count, candidates);

                if (candidates_count > 0 && can_be_saved(cb, g)) {
                    for (u16 j = 0; j < candidates_count; ++j) {
                        capturable[candidates[j]] += g->stones.count + g->liberties;
                    }
                }
            }
        }
    }

    u8 libs_after_playing[TOTAL_BOARD_SIZ];
    memset(libs_after_playing, 0, TOTAL_BOARD_SIZ);

    move ko = get_ko_play(cb);
    stats->plays_count = 0;

    for (move k = 0; k < cb->empty.count; ++k) {
        move m = cb->empty.coord[k];

        /*
        Don't play intersections disqualified because of a better, nearby nakade
        or because they are eyes
        */
        if (!viable[m]) {
            continue;
        }

        /*
        Ko violation
        */
        if (ko == m) {
            continue;
        }

        move _ignored;
        u8 libs = libs_after_play(cb, is_black, m, &_ignored);

        /*
        Don't play suicides
        */
        if (libs == 0) {
            continue;
        }

        libs_after_playing[m] = libs;

        /*
        Even game heuristic
        */
        u32 mc_w = prior_even;
        u32 mc_v = prior_even * 2;

        /*
        Avoid typically poor plays like eye shape
        */
        if (!play_okay[m]) {
            mc_v += prior_bad_play;
        } else {
            /*
            Avoid safe tiger mouths.
            */
            if (safe_tigers_mouth(cb, is_black, m)) {
                mc_v += prior_bad_play;
            }
        }

        if (out_neighbors4[m] == 2 && ((is_black && cb->white_neighbors8[m] == 0) || (!is_black && cb->black_neighbors8[m] == 0))) {
            mc_v += prior_bad_play;
        }

        /*
        Prohibit self-ataris that don't contribute to killing an opponent group
        */
        if (capturable[0] == 0 && (libs < 2 && lib2_self_atari(cb, is_black, m))) {
            mc_v += prior_self_atari;
        }

        /*
        Nakade
        */
        if (in_nakade[m] > 0) {
            group * g = get_closest_group(cb, m);
            if (g->eyes < 2) { /* nakade eye shape is already an eye */
                u16 b = (u16)powf(in_nakade[m], prior_stone_scale_factor);
                mc_w += prior_nakade + b;
                mc_v += prior_nakade + b;
            }
        }

        /*
        Saving plays
        */
        if (saving_play[m] > 0) {
            u16 b = (u16)powf(saving_play[m], prior_stone_scale_factor);
            mc_w += prior_defend + b;
            mc_v += prior_defend + b;
        }

        /*
        Capturing plays
        */
        if (capturable[m] > 0) {
            u16 b = (u16)powf(capturable[m], prior_stone_scale_factor);
            mc_w += prior_attack + b;
            mc_v += prior_attack + b;
        }

        /*
        3x3 patterns
        */
        if (libs > 1 && pat3_find(cb->hash[m], is_black) != 0) {
            mc_w += prior_pat3;
            mc_v += prior_pat3;
        }


        /*
        Favor plays near to the last and its group liberties
        */
        if (near_last_play[m]) {
            mc_w += prior_near_last;
            mc_v += prior_near_last;
        }


        /*
        Bonuses based on line and empty parts of the board
        */
        if (stones_in_manhattan_dst3(cb, m) == 0) {
            u8 dst_border = distances_to_border[m];
            switch (dst_border) {
                case 0:
                    /* Do not play there at all */
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

            if (is_starting[m]) {
                mc_w += prior_starting_point;
                mc_v += prior_starting_point;
            }
        }


        /*
        Corner of the board bonus
        */
        if (out_neighbors4[m] == 2) {
            mc_v += prior_corner;
        }


        stats_add_play_tmp(stats, m, mc_w, mc_v);
    }

    /*
    Transform win/visits into quality/visits statistics and copy MC to
    AMAF/RAVE statistics
    */
    for (u16 i = 0; i < stats->plays_count; ++i) {
        tt_play * play = &stats->plays[i];
        play->amaf_q = play->mc_q = play->mc_q / play->mc_n;
        play->amaf_n = play->mc_n;
    }

    /*
    Add pass simulation
    */
    if (cb->empty.count < TOTAL_BOARD_SIZ / 2 || stats->plays_count < TOTAL_BOARD_SIZ / 8) {
        stats_add_play_final(stats, PASS, UCT_RESIGN_WINRATE, prior_pass);
    }
}
