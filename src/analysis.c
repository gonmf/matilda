/*
Functions for human-like analysis and ponderation on the game.

This is very incomplete so far.
*/

#include "matilda.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "alloc.h"
#include "board.h"
#include "mcts.h"
#include "timem.h"
#include "transpositions.h"
#include "types.h"
#include "zobrist.h"

static tt_play * select_best(
    tt_stats * stats
){
    if(stats->plays_count == 0)
        return NULL;

    tt_play * ret = &stats->plays[0];
    for(move i = 1; i < stats->plays_count; ++i)
        if(ret->mc_q < stats->plays[i].mc_q)
    ret = &stats->plays[i];

    return ret;
}

static void _print_sequence(
    char ** buf,
    tt_play * p
){
    char * tmp = alloc();
    coord_to_alpha_num(tmp, p->m);
    *buf += snprintf(*buf, 8, " %s", tmp);
    release(tmp);

    tt_stats * stats = (tt_stats *)p->next_stats;
    if(stats == NULL)
        return;
    tt_play * best_play = select_best(stats);
    if(best_play == NULL)
        return;
    _print_sequence(buf, best_play);
}

static void print_sequence(
    char ** buf,
    tt_play * p
){
    tt_stats * stats = (tt_stats *)p->next_stats;
    if(stats == NULL)
        return; /* may be null if play is a pass */
    tt_play * best_play = select_best(stats);

    char * tmp = alloc();
    coord_to_alpha_num(tmp, p->m);

    if(best_play == NULL){
        *buf += snprintf(*buf, 8, "%s\n", tmp);
        release(tmp);
        return;
    }

    *buf += snprintf(*buf, 32, "%s followed by", tmp);
    release(tmp);
    _print_sequence(buf, best_play);
    *buf += snprintf(*buf, 4, "\n");
}

/*
Produces a textual opinion on the best followup, given the time available to
think.
*/
void request_opinion(
    char * dst,
    const board * b,
    bool is_black,
    u64 milliseconds
){
    transpositions_table_init();

    u64 zobrist_hash = zobrist_new_hash(b);
    out_board ignored;

    u64 curr_time = current_time_in_millis();
    u64 stop_time = curr_time + milliseconds;
    mcts_start_timed(&ignored, b, is_black, stop_time, stop_time);
    tt_stats * stats = transpositions_lookup_create(b, is_black, zobrist_hash);
    omp_unset_lock(&stats->lock);
    if(stats->expansion_delay != -1)
        return;

    u8 play_count = 0;
    tt_play * best_plays[5];

    for(move i = 0; i < stats->plays_count; ++i)
        if(play_count < 5)
        {
            best_plays[play_count] = &stats->plays[i];
            play_count++;
        }
        else
        {
            double q = stats->plays[i].mc_q;
            for(u8 j = 0; j < 5; ++j)
                if(best_plays[j]->mc_q < q)
                {
                    best_plays[j] = &stats->plays[i];
                    break;
                }
        }

    if(play_count == 0)
    {
        dst += snprintf(dst, MAX_PAGE_SIZ,
            "There are no available plays for %s.\n", is_black ? "black" :
            "white");
        return;
    }

    bool r;
    do
    {
        r = false;
        for(u8 j = 1; j < play_count; ++j)
            if(best_plays[j - 1]->mc_q < best_plays[j]->mc_q)
            {
                tt_play * tmp = best_plays[j];
                best_plays[j] = best_plays[j - 1];
                best_plays[j - 1] = tmp;
                r = true;
            }
    }
    while(r);

    if(best_plays[0]->mc_q > 0.7)
    {
        dst += snprintf(dst, MAX_PAGE_SIZ, "%s has won the game.\n", is_black ?
            "Black" : "White");
        return;
    }

    if(best_plays[0]->mc_q > 0.63)
    {
        dst += snprintf(dst, MAX_PAGE_SIZ, "%s is winning the game.\n", is_black
            ? "Black" : "White");
        goto continue_lbl;
    }

    if(best_plays[0]->mc_q > 0.55)
    {
        dst += snprintf(dst, MAX_PAGE_SIZ, "%s is ahead in the game.\n",
            is_black ? "Black" : "White");
        goto continue_lbl;
    }

    if(best_plays[0]->mc_q > 0.5)
    {
        dst += snprintf(dst, MAX_PAGE_SIZ,
            "The players are very close, but %s has the advantage.\n", is_black
            ? "black" : "white");
        goto continue_lbl;
    }

    if(best_plays[0]->mc_q > 0.45)
    {
        dst += snprintf(dst, MAX_PAGE_SIZ, "The players are very close.\n");
        goto continue_lbl;
    }

    if(best_plays[0]->mc_q > 0.4)
    {
        dst += snprintf(dst, MAX_PAGE_SIZ, "%s is ahead in the game.\n",
            !is_black ? "Black" : "White");
        goto continue_lbl;
    }

    if(best_plays[0]->mc_q > 0.3)
    {
        dst += snprintf(dst, MAX_PAGE_SIZ,  "%s is winning the game.\n",
            !is_black ? "Black" : "White");
        goto continue_lbl;
    }

    dst += snprintf(dst, MAX_PAGE_SIZ, "%s has won the game.\n", !is_black ?
        "Black" : "White");
    return;

continue_lbl: ;

    double q = best_plays[0]->mc_q;
    for(u8 i = 1; i < play_count; ++i)
        if(fabs(best_plays[i]->mc_q - q) > 0.02)
            play_count = i;

    if(play_count == 1)
    {
        dst += snprintf(dst, MAX_PAGE_SIZ, "The best play is ");
        print_sequence(&dst, best_plays[0]);
    }
    else
    {
        dst += snprintf(dst, MAX_PAGE_SIZ, "The best plays are:\n");
        for(u8 i = 0; i < play_count; ++i)
            print_sequence(&dst, best_plays[i]);
    }
}
