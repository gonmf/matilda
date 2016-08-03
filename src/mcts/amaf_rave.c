/*
Functions that deal with updating AMAF information and its use in MC-RAVE.

Uses a minimum MSE schedule.

AMAF traversions are marked EMPTY when not visited, BLACK_STONE for first
visited by black and WHITE_STONE for first visited by white.
*/

#include "matilda.h"

#include <stdlib.h>
#include <math.h>

#include "amaf_rave.h"
#include "types.h"

double rave_mse_b = RAVE_MSE_B;
static double rave_mse_b_sqrd = RAVE_MSE_B * RAVE_MSE_B * 4.0;

/*
Optional initiation routine in case rave_mse_b is modified for optimization.
*/
void amaf_rave_init()
{
    rave_mse_b_sqrd = rave_mse_b * rave_mse_b * 4.0;
}

/*
Calculation of the RAVE value of a state transition.
RETURNS overall value of play (x,y)
*/
double uct1_rave(
    const tt_play * play
){
    u32 n_amaf_s_a;
    double q_amaf_s_a;

#if CRITICALITY_THRESHOLD > 0
    if(play->mc_n >= CRITICALITY_THRESHOLD)
    {
        double c_pachi = play->owner_winning - (2.0 * play->color_owning *
            play->mc_q - play->color_owning - play->mc_q + 1.0);
        double crit_n = fabs(c_pachi) * play->amaf_n;

        n_amaf_s_a = play->amaf_n + crit_n;
        if(c_pachi <= 0.0)
            q_amaf_s_a = play->amaf_q;
        else
            q_amaf_s_a = ((play->amaf_q * play->amaf_n) + crit_n) / n_amaf_s_a;
    }
    else
    {
#endif
        n_amaf_s_a = play->amaf_n;
        q_amaf_s_a = play->amaf_q;
#if CRITICALITY_THRESHOLD > 0
    }
#endif

    /* RAVE minimum MSE schedule */
    double b = n_amaf_s_a / (play->mc_n + n_amaf_s_a + play->mc_n *
        n_amaf_s_a * rave_mse_b_sqrd);

    return (1.0 - b) * play->mc_q + b * q_amaf_s_a;
}

/*
Batch update of all transitions that were visited anytime after the current
state (if visited first by the player).
*/
void update_amaf_stats(
    tt_stats * stats,
    const u8 traversed[BOARD_SIZ * BOARD_SIZ],
    bool is_black,
    double z
){
    for(u16 k = 0; k < stats->plays_count; ++k)
        if(traversed[stats->plays[k].m] != EMPTY &&
            (traversed[stats->plays[k].m] == BLACK_STONE) == is_black)
        {
            stats->plays[k].amaf_n++;
            stats->plays[k].amaf_q += ((z - stats->plays[k].amaf_q) /
                stats->plays[k].amaf_n);
        }
}

/*
Batch update of all transitions that were visited anytime after the current
state (if visited first by the player).
This versions only adds losses -- is meant to use when a draw occurs.
*/
void update_amaf_stats2(
    tt_stats * stats,
    const u8 traversed[BOARD_SIZ * BOARD_SIZ],
    bool is_black
){
    for(u16 k = 0; k < stats->plays_count; ++k)
        if(traversed[stats->plays[k].m] != EMPTY &&
            (traversed[stats->plays[k].m] == BLACK_STONE) == is_black)
        {
            stats->plays[k].amaf_n++;
            stats->plays[k].amaf_q -= (stats->plays[k].amaf_q /
                stats->plays[k].amaf_n);
        }
}

