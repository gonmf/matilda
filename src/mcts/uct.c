/*
Heuristic UCT-RAVE implementation.

With UCB1-TUNED, RAVE and criticality.
Playout is limited with dynamic offset depending on stone count.
Cutoff playouts are rated. Playouts are cut short with a mercy threshold (like
pachi, orego and others).
Initilizes expanded states with prior values.
Last-good-reply with forgetting (LGRF1) is also used.
A virtual loss is also added on play traversion, that is later corrected if
needed.

MCTS can be resumed on demand by a few extra simulations at a time.
It can also record the average final score, for the purpose of score estimation.
*/

#include "matilda.h"

#include <stdio.h>
#include <string.h>
#include <math.h> /* for round, sqrt */
#include <stdlib.h>
#include <assert.h>
#include <omp.h>

#include "alloc.h"
#include "amaf_rave.h"
#include "board.h"
#include "cfg_board.h"
#include "flog.h"
#include "mcts.h"
#include "move.h"
#include "pat3.h"
#include "playout.h"
#include "priors.h"
#include "pts_file.h"
#include "randg.h"
#include "scoring.h"
#include "state_changes.h"
#include "timem.h"
#include "transpositions.h"
#include "types.h"
#include "constants.h"
#include "zobrist.h"

/*
Public to allow parameter optimization
*/
double ucb1_c = UCB1_C;

/* from board_constants */
extern u8 distances_to_border[TOTAL_BOARD_SIZ];
extern move_seq nei_dst_3[TOTAL_BOARD_SIZ];

static bool ran_out_of_memory;
static bool search_stop;
static u16 max_depths[MAXIMUM_NUM_THREADS];


/*
Whether a MCTS can be started on background. Is disabled if memory runs out, and
needs to be reset before testing again if can be run.
*/
static bool mcts_can_resume = true;



static bool uct_inited = false;
static void mcts_init()
{
    if(uct_inited)
        return;

    rand_init();
    board_constants_init();
    zobrist_init();
    pat3_init();
    transpositions_table_init();
    amaf_rave_init();

    uct_inited = true;
}



/*
For limiting the branching actor by a certain distance from an already place
stone.
*/
#if USE_UCT_BRANCH_LIMITER
static bool enable_branch_limit = false;

static void update_branch_limit(
    bool b[TOTAL_BOARD_SIZ],
    move m
){
    for(u16 n = 0; n < nei_dst_3[m].count; ++n)
    {
        move m2 = nei_dst_3[m].coord[n];
        b[m2] = true;
    }
}

static void init_branch_limit(
    const u8 p[TOTAL_BOARD_SIZ],
    bool b[TOTAL_BOARD_SIZ]
){
    move_seq ms;
    get_starting_points(&ms);

    memset(b, false, TOTAL_BOARD_SIZ);
    for(move m = 0; m < TOTAL_BOARD_SIZ; ++m)
        if(p[m] != EMPTY)
            update_branch_limit(b, m);

    for(move i = 0; i < ms.count; ++i)
    {
        move m = ms.coord[i];
        if(p[m] == EMPTY)
            b[m] = true;
    }
}
#endif


static void select_play(
    tt_stats * stats,
    tt_play ** play
){
    if(*play != NULL && (*play)->lgrf1_reply != NULL)
    {
        *play = (*play)->lgrf1_reply;
        return;
    }

    tt_play * best_plays[TOTAL_BOARD_SIZ];
    double best_q = -1.0;
    u16 equal_quality_plays = 0;

    double log_n = log(stats->mc_n_total);
    for(move k = 0; k < stats->plays_count; ++k)
    {
#if USE_AMAF_RAVE
        double play_q = uct1_rave(&stats->plays[k]);
#else
        double play_q = stats->plays[k].mc_q;
#endif

        /* UCB1-TUNED biasing */
        double log_n_jn = log_n / stats->plays[k].mc_n;
        double ucb_v = play_q - (play_q * play_q) + sqrt(2.0 * log_n_jn);
        double ucb_bias = sqrt(log_n_jn * MIN(0.25, ucb_v));

        /* UCT */
        double uct_q = play_q + ucb1_c * ucb_bias;
        if(uct_q > best_q){
            best_plays[0] = &stats->plays[k];
            equal_quality_plays = 1;
            best_q = uct_q;
        }
        else
            if(uct_q == best_q)
            {
                best_plays[equal_quality_plays] = &stats->plays[k];
                ++equal_quality_plays;
            }
    }

    if(equal_quality_plays == 1)
    {
        *play = best_plays[0];
        return;
    }

    if(equal_quality_plays > 1)
    {
        u16 p = rand_u16(equal_quality_plays);
        *play = best_plays[p];
        return;
    }

    flog_crit("mcts", "play selection exception");
}

static d16 mcts_expansion(
    cfg_board * cb,
    bool is_black,
    tt_stats * stats,
    const bool branch_limit[TOTAL_BOARD_SIZ],
    u8 traversed[TOTAL_BOARD_SIZ]
){
    stats->expansion_delay--;
    if(stats->expansion_delay == -1)
        init_new_state(cb, stats, is_black, branch_limit);
    omp_unset_lock(&stats->lock);
    d16 outcome = playout_heavy_amaf(cb, is_black, traversed);

    return outcome;
}

static d16 mcts_selection(
    cfg_board * cb,
    u64 zobrist_hash,
    bool is_black,
    bool branch_limit[TOTAL_BOARD_SIZ]
){

    d16 depth = 6;
    tt_stats * stats[MAX_UCT_DEPTH + 6];
    tt_play * plays[MAX_UCT_DEPTH + 7];
    /* for testing superko */
    stats[0] = stats[1] = stats[2] = stats[3] = stats[4] = stats[5] = NULL;

    u8 traversed[TOTAL_BOARD_SIZ];
    memset(traversed, EMPTY, TOTAL_BOARD_SIZ);

    d16 outcome;
    tt_stats * curr_stats = NULL;
    tt_play * play = NULL;

    while(1)
    {
        if(depth >= MAX_UCT_DEPTH + 6)
        {
            outcome = score_stones_and_area(cb->p);
            break;
        }

        if(curr_stats == NULL)
        {
            curr_stats = transpositions_lookup_null(cb, is_black, zobrist_hash);

            if(curr_stats == NULL)
            {
                if(!ran_out_of_memory)
                {
                    ran_out_of_memory = true;
                    search_stop = true;
                }
                outcome = playout_heavy_amaf(cb, is_black, traversed);
                break;
            }
            else
            {
                if(play != NULL)
                    play->next_stats = curr_stats;
            }
        }
        else
            omp_set_lock(&curr_stats->lock);

        /* Superko detection */
        if(is_board_move(cb->last_played) && (stats[depth - 4] == curr_stats ||
            stats[depth - 6] == curr_stats))
        {
            omp_unset_lock(&curr_stats->lock);
            /* loss for player that committed superko */
            outcome = is_black ? 1 : -1;
            break;
        }

        if(curr_stats->expansion_delay >= 0)
        {
            outcome = mcts_expansion(cb, is_black, curr_stats, (const bool
                *)branch_limit, traversed); /* already unsets lock */
            break;
        }

        select_play(curr_stats, &play);

        curr_stats->mc_n_total++;
        play->mc_n++;
        play->mc_q -= play->mc_q / play->mc_n;
        omp_unset_lock(&curr_stats->lock);

        if(play->m == PASS)
        {
            if(cb->last_played == PASS)
            {
                outcome = score_stones_and_area(cb->p);
                break;
            }
            just_pass(cb);
        }
        else
        {
#if USE_UCT_BRANCH_LIMITER
            if(enable_branch_limit)
                update_branch_limit(branch_limit, play->m);
#endif
            just_play2(cb, play->m, is_black, &zobrist_hash);
        }

        plays[depth] = play;
        stats[depth] = curr_stats;
        ++depth;
        curr_stats = play->next_stats;
        is_black = !is_black;
    }

    if(outcome == 0)
    {
        for(d16 k = depth - 1; k >= 6; --k)
        {
            is_black = !is_black;
            move m = plays[k]->m;
            omp_set_lock(&stats[k]->lock);

            /* LGRF */
            plays[k]->lgrf1_reply = NULL;

            /* AMAF/RAVE */
            if(m != PASS)
                traversed[m] = is_black ? BLACK_STONE : WHITE_STONE;
            update_amaf_stats2(stats[k], traversed, is_black);
            omp_unset_lock(&stats[k]->lock);
        }
    }
    else
    {
        plays[depth] = NULL;
        for(d16 k = depth - 1; k >= 6; --k)
        {
            is_black = !is_black;
            move m = plays[k]->m;
            double z = (is_black == (outcome > 0)) ? 1.0 : 0.0;

            omp_set_lock(&stats[k]->lock);
            /* MC sampling */
            if(is_black == (outcome > 0))
                plays[k]->mc_q += 1.0 / plays[k]->mc_n;

            /* AMAF/RAVE */
            if(m != PASS)
                traversed[m] = is_black ? BLACK_STONE : WHITE_STONE;
            update_amaf_stats(stats[k], traversed, is_black, z);

            /* LGRF */
            if(is_black == (outcome > 0))
                plays[k]->lgrf1_reply = NULL;
            else
                plays[k]->lgrf1_reply = plays[k + 1];

            /* Criticality */
            if(m != PASS && cb->p[m] != EMPTY)
            {
                double winner_owns_coord = ((outcome > 0) == (cb->p[m] ==
                    BLACK_STONE)) ? 1.0 : 0.0;
                plays[k]->owner_winning += (winner_owns_coord -
                    plays[k]->owner_winning) / plays[k]->mc_n;
                double player_owns_coord = (is_black == (cb->p[m] ==
                    BLACK_STONE)) ? 1.0 : 0.0;
                plays[k]->color_owning += (player_owns_coord -
                    plays[k]->color_owning) / plays[k]->mc_n;
            }

            omp_unset_lock(&stats[k]->lock);
        }
    }

    depth -= 6;
    if(depth > max_depths[omp_get_thread_num()])
        max_depths[omp_get_thread_num()] = depth;

    return outcome;
}

/*
Performs a MCTS in at least the available time.

The search may end early if the estimated win rate is very one sided, in which
case the play selected is a pass. The search is also interrupted if memory runs
out.
RETURNS true if a play or pass is suggested instead of resigning
*/
bool mcts_start(
    out_board * out_b,
    const board * b,
    bool is_black,
    u64 stop_time,
    u64 early_stop_time
){
    /* really just to mute unused parameter warnings */
    if(early_stop_time > stop_time)
        flog_crit("uct", "illegal values for stoppage times");

    mcts_init();

    bool start_branch_limit[TOTAL_BOARD_SIZ];
#if USE_UCT_BRANCH_LIMITER
    /* ignore branch limiting if too many stones on the board */
    u16 stones = stone_count(b->p);
    if(stones <= TOTAL_BOARD_SIZ / 3)
    {
        init_branch_limit(b->p, start_branch_limit);
        enable_branch_limit = true;
    }
    else
    {
        memset(start_branch_limit, true, TOTAL_BOARD_SIZ);
        enable_branch_limit = false;
    }
#endif

    u64 start_zobrist_hash = zobrist_new_hash(b);
    tt_stats * stats = transpositions_lookup_create(b, is_black,
        start_zobrist_hash);
    omp_unset_lock(&stats->lock);

    cfg_board initial_cfg_board;
    cfg_from_board(&initial_cfg_board, b);

    if(stats->expansion_delay != -1)
    {
        stats->expansion_delay = -1;
        init_new_state(&initial_cfg_board, stats, is_black, start_branch_limit);
    }

    memset(max_depths, 0, sizeof(u16) * MAXIMUM_NUM_THREADS);

    u32 draws = 0;
    u32 wins = 0;
    u32 losses = 0;

    ran_out_of_memory = false;
    search_stop = false;
    bool stopped_early_by_wr = false;

#if LIMIT_BY_PLAYOUTS

    #pragma omp parallel for
    for(u32 sim = 0; sim < PLAYOUTS_PER_TURN; ++sim)
    {
        bool branch_limit[TOTAL_BOARD_SIZ];
#if USE_UCT_BRANCH_LIMITER
        memcpy(branch_limit, start_branch_limit, TOTAL_BOARD_SIZ);
#endif

        cfg_board cb;
        cfg_board_clone(&cb, &initial_cfg_board);
        d16 outcome = mcts_selection(&cb, start_zobrist_hash, is_black,
            branch_limit);
        cfg_board_free(&cb);
        if(outcome == 0)
        {
            #pragma omp atomic
            draws++;
        }
        else
        {
            if((outcome > 0) == is_black)
            {
                #pragma omp atomic
                wins++;
            }
            else
            {
                #pragma omp atomic
                losses++;
            }
        }
    }

#else

    do
    {
        #pragma omp parallel for
        for(u32 sim = 0; sim < 960; ++sim)
        {
            if(search_stop)
                continue;

            bool branch_limit[TOTAL_BOARD_SIZ];
#if USE_UCT_BRANCH_LIMITER
            memcpy(branch_limit, start_branch_limit, TOTAL_BOARD_SIZ);
#endif

            cfg_board cb;
            cfg_board_clone(&cb, &initial_cfg_board);
            d16 outcome = mcts_selection(&cb, start_zobrist_hash, is_black,
                branch_limit);
            cfg_board_free(&cb);
            if(outcome == 0)
            {
                #pragma omp atomic
                draws++;
            }
            else
            {
                if((outcome > 0) == is_black)
                {
                    #pragma omp atomic
                    wins++;
                }
                else
                {
                    #pragma omp atomic
                    losses++;
                }
            }

            if(omp_get_thread_num() == 0)
            {
                u64 curr_time = current_time_in_millis();

#if UCT_CAN_STOP_EARLY
                if(curr_time >= early_stop_time)
                {
                    if(curr_time >= stop_time)
                    {
                        search_stop = true;
                        continue;
                    }
                    double wr = ((double)wins) / ((double)(wins + losses));
                    if(wr >= UCT_EARLY_WINRATE)
                    {
                        stopped_early_by_wr = true;
                        search_stop = true;
                    }
                }
#else
                if(curr_time >= stop_time)
                {
                    search_stop = true;
                    continue;
                }
#endif
            }
        }


    }
    while(!search_stop);

#endif

    if(ran_out_of_memory)
        flog_warn("uct", "search ran out of memory");

    char * s = alloc();
    if(stopped_early_by_wr)
    {
        d64 diff = stop_time - current_time_in_millis();
        snprintf(s, MAX_PAGE_SIZ, "search ended %" PRId64 "ms early", diff);
        flog_info("uct", s);
    }

    clear_out_board(out_b);
    out_b->pass = UCT_RESIGN_WINRATE;
    for(move k = 0; k < stats->plays_count; ++k)
    {
        if(stats->plays[k].m == PASS)
        {
            out_b->pass = stats->plays[k].mc_q;
        }
        else
        {
            out_b->tested[stats->plays[k].m] = true;
            out_b->value[stats->plays[k].m] = stats->plays[k].mc_q;
        }
    }

    u16 max_depth = max_depths[0];
    for(u16 k = 1; k < MAXIMUM_NUM_THREADS; ++k)
        if(max_depths[k] > max_depth)
            max_depth = max_depths[k];

    u32 sims = wins + losses;
    double wr = ((double)wins) / ((double)sims);

    if(draws > 0)
    {
        snprintf(s, MAX_PAGE_SIZ, "search finished (sims=%u, depth=%u, wr\
=%.2f, draws=%u)\n", sims, max_depth, wr, draws);
    }
    else
    {
        snprintf(s, MAX_PAGE_SIZ, "search finished (sims=%u, depth=%u, wr\
=%.2f)\n", sims, max_depth, wr);
    }
    flog_info("uct", s);

    release(s);
    cfg_board_free(&initial_cfg_board);

    /* prevent resignation if we have played very few simulations */
    if(sims >= UCT_RESIGN_PLAYOUTS && wr < UCT_RESIGN_WINRATE)
        return false;

    return true;
}

/*
Reset whether MCTS can run in the background after a previous attempt may have
run out of memory.
*/
void reset_mcts_can_resume()
{
    mcts_can_resume = true;
}

/*
Continue a previous MCTS.
*/
void mcts_resume(
    const board * b,
    bool is_black
){
    if(!mcts_can_resume)
        return;

    mcts_init();

    u64 stop_time = current_time_in_millis() + 100;
    ran_out_of_memory = false;
    search_stop = false;

    u64 start_zobrist_hash = zobrist_new_hash(b);
    bool start_branch_limit[TOTAL_BOARD_SIZ];
#if USE_UCT_BRANCH_LIMITER
     /* ignore branch limiting if too many stones on the board */
    u16 stones = stone_count(b->p);
    if(stones <= TOTAL_BOARD_SIZ / 3)
    {
        init_branch_limit(b->p, start_branch_limit);
        enable_branch_limit = true;
    }
    else
    {
        memset(start_branch_limit, true, TOTAL_BOARD_SIZ);
        enable_branch_limit = false;
    }
#endif

    cfg_board initial_cfg_board;
    cfg_from_board(&initial_cfg_board, b);

    do
    {

        #pragma omp parallel for
        for(u32 sim = 0; sim < 960; ++sim)
        {
            if(search_stop)
                continue;

            bool branch_limit[TOTAL_BOARD_SIZ];
#if USE_UCT_BRANCH_LIMITER
            memcpy(branch_limit, start_branch_limit, TOTAL_BOARD_SIZ);
#endif

            cfg_board cb;
            cfg_board_clone(&cb, &initial_cfg_board);
            mcts_selection(&cb, start_zobrist_hash, is_black, branch_limit);
            cfg_board_free(&cb);

            if(omp_get_thread_num() == 0)
            {
                u64 curr_time = current_time_in_millis();
                if(curr_time >= stop_time)
                    search_stop = true;
            }
        }

    }
    while(!search_stop);

    if(ran_out_of_memory)
        mcts_can_resume = false;

    cfg_board_free(&initial_cfg_board);
}


