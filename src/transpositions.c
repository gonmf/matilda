/*
Transpositions table and tree implementation.

Doesn't assume states are in reduced form. States contain full information and
are compared after the hash (collisions are impossible). Zobrist hashing with 64
bits is used. Clean-up is available only between turns or between games.

Please note there is no separate 'UCT state information' file. It is mostly
interweaved with the transpositions table.

The table is actually two tables, one for each player. Mixing their statistics
is illegal. The nodes statistics are from the perspective of the respective
table color.
*/

#include "matilda.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <omp.h>

#include "alloc.h"
#include "board.h"
#include "cfg_board.h"
#include "flog.h"
#include "primes.h"
#include "transpositions.h"
#include "types.h"
#include "zobrist.h"

u16 expansion_delay = UCT_EXPANSION_DELAY;
u64 max_size_in_mbs = DEFAULT_UCT_MEMORY;

static u32 max_allocated_states;
static u32 number_of_buckets;

static u32 allocated_states = 0;
static u32 states_in_use = 0;

static omp_lock_t freed_nodes_lock;
/* locks for every bucket of the hash table */
static omp_lock_t * b_locks_table = NULL;
static omp_lock_t * w_locks_table = NULL;
static tt_stats ** b_stats_table = NULL;
static tt_stats ** w_stats_table = NULL;
static tt_stats * freed_nodes = NULL;

/* value used to mark items for deletion; will cycle eventually but its not a
big deal */
static u8 maintenance_mark = 0;


/*
Initialize the transpositions table structures.
*/
void transpositions_table_init()
{
    if(b_stats_table == NULL)
    {
        u64 mbs = max_size_in_mbs;
        mbs *= 1048576;

        max_allocated_states = mbs / sizeof(tt_stats);
        number_of_buckets = get_prime_near(max_allocated_states / 2);

        b_stats_table = (tt_stats **)calloc(number_of_buckets,
            sizeof(tt_stats *));
        if(b_stats_table == NULL)
            flog_crit("tt", "system out of memory");

        b_locks_table = (omp_lock_t *)malloc(number_of_buckets *
            sizeof(omp_lock_t));
        if(b_locks_table == NULL)
            flog_crit("tt", "system out of memory");

        w_stats_table = (tt_stats **)calloc(number_of_buckets,
            sizeof(tt_stats *));
        if(w_stats_table == NULL)
            flog_crit("tt", "system out of memory");

        w_locks_table = (omp_lock_t *)malloc(number_of_buckets *
            sizeof(omp_lock_t));
        if(w_locks_table == NULL)
            flog_crit("tt", "system out of memory");

        for(u32 i = 0; i < number_of_buckets; ++i)
        {
            omp_init_lock(&b_locks_table[i]);
            omp_init_lock(&w_locks_table[i]);
        }
        omp_init_lock(&freed_nodes_lock);
    }
}

/*
Searches for a state by hash, in a bucket by key.
RETURNS state found or null.
*/
static tt_stats * find_state(
    u64 hash,
    const board * b,
    bool is_black
){
    u32 key = (u32)(hash % ((u64)number_of_buckets));
    tt_stats * p;

    if(is_black)
        p = b_stats_table[key];
    else
        p = w_stats_table[key];

    bool last_passed = (b->last_played == PASS);

    while(p != NULL)
    {
        if(p->zobrist_hash == hash && memcmp(p->p, b->p, TOTAL_BOARD_SIZ)
            == 0 && p->last_eaten == b->last_eaten && p->last_passed ==
            last_passed)
            return p;

        p = p->next;
    }

    return NULL;
}

static tt_stats * find_state2(
    u64 hash,
    const cfg_board * cb,
    bool is_black
){
    u32 key = (u32)(hash % ((u64)number_of_buckets));
    tt_stats * p;

    if(is_black)
        p = b_stats_table[key];
    else
        p = w_stats_table[key];

    bool last_passed = (cb->last_played == PASS);

    while(p != NULL)
    {
        if(p->zobrist_hash == hash && memcmp(p->p, cb->p, TOTAL_BOARD_SIZ)
            == 0 && p->last_eaten == cb->last_eaten && p->last_passed ==
            last_passed)
            return p;

        p = p->next;
    }

    return NULL;
}

static tt_stats * create_state(
    u64 hash
){
    tt_stats * ret = NULL;

    omp_set_lock(&freed_nodes_lock);
    if(freed_nodes != NULL)
    {
        ret = freed_nodes;
        freed_nodes = freed_nodes->next;
    }
    else
        ++allocated_states;
    ++states_in_use;
    omp_unset_lock(&freed_nodes_lock);

    if(ret == NULL)
    {
        ret = (tt_stats *)malloc(sizeof(tt_stats));
        if(ret == NULL)
            flog_crit("tt", "create_state: system out of memory");

        omp_init_lock(&ret->lock);
    }

    /* careful that some fields are not initialized here */
    ret->zobrist_hash = hash;
    ret->maintenance_mark = maintenance_mark;
    ret->plays_count = 0;
    ret->expansion_delay = expansion_delay;
    return ret;
}

static void release_state(
    tt_stats * s
){
    --states_in_use;
    s->next = freed_nodes;
    freed_nodes = s;
}

static void release_states_not_marked()
{
    for(u32 i = 0; i < number_of_buckets; ++i)
    {
        /* black table */
        while(b_stats_table[i] != NULL && b_stats_table[i]->maintenance_mark !=
            maintenance_mark)
        {
            tt_stats * tmp = b_stats_table[i]->next;
            release_state(b_stats_table[i]);
            b_stats_table[i] = tmp;
        }
        if(b_stats_table[i] != NULL)
        {
            tt_stats * prev = b_stats_table[i];
            tt_stats * curr = prev->next;
            while(curr != NULL)
            {
                if(curr->maintenance_mark != maintenance_mark)
                {
                    tt_stats * tmp = curr->next;
                    release_state(curr);
                    prev->next = tmp;
                    curr = tmp;
                }
                else
                {
                    prev = curr;
                    curr = curr->next;
                }
            }
        }

        /* white table */
        while(w_stats_table[i] != NULL && w_stats_table[i]->maintenance_mark !=
            maintenance_mark)
        {
            tt_stats * tmp = w_stats_table[i]->next;
            release_state(w_stats_table[i]);
            w_stats_table[i] = tmp;
        }
        if(w_stats_table[i] != NULL)
        {
            tt_stats * prev = w_stats_table[i];
            tt_stats * curr = prev->next;
            while(curr != NULL)
            {
                if(curr->maintenance_mark != maintenance_mark)
                {
                    tt_stats * tmp = curr->next;
                    release_state(curr);
                    prev->next = tmp;
                    curr = tmp;
                }
                else
                {
                    prev = curr;
                    curr = curr->next;
                }
            }
        }
    }
}

static void mark_states_for_keeping(
    tt_stats * s
){
    if(s->maintenance_mark == maintenance_mark)
        return;

    s->maintenance_mark = maintenance_mark;

    for(move i = 0; i < s->plays_count; ++i)
        if(s->plays[i].next_stats != NULL)
            mark_states_for_keeping(s->plays[i].next_stats);
}

/*
Frees states outside of the subtree started at state b. Not thread-safe.
RETURNS number of states freed.
*/
u32 tt_clean_outside_tree(
    const board * b,
    bool is_black
){
    u64 hash = zobrist_new_hash(b);
    u32 states_in_use_before = states_in_use;
    tt_stats * stats = find_state(hash, b, is_black);
    if(stats == NULL) /* free all */
        tt_clean_all();
    else
    {
        /* free outside tree */
        ++maintenance_mark;
        mark_states_for_keeping(stats);
        release_states_not_marked();
    }

    d32 states_released = states_in_use_before - states_in_use;
    assert(states_released >= 0);
    return states_released;
}

/*
Looks up a previously stored state, or generates a new one. No assumptions are
made about whether the board state is in reduced form already. Never fails. If
the memory is full it allocates a new state regardless. If the state is found
and returned it's OpenMP lock is first set. Thread-safe.
RETURNS the state information
*/
tt_stats * transpositions_lookup_create(
    const board * b,
    bool is_black,
    u64 hash
){
    u32 key = (u32)(hash % ((u64)number_of_buckets));
    omp_lock_t * bucket_lock = is_black ? &b_locks_table[key] :
        &w_locks_table[key];
    omp_set_lock(bucket_lock);

    tt_stats * ret = find_state(hash, b, is_black);
    if(ret == NULL) /* doesnt exist */
    {
        if(states_in_use >= max_allocated_states)
        {
            /*
            It is possible in theory for a complex ko to produce a situation
            where freeing the game tree that is not reachable doesn't free any
            states.
            */
            transpositions_log_status();
            char * s = alloc();
            board_to_string(s, b->p, b->last_played, b->last_eaten);
            flog_warn("tt", s);
            release(s);
            flog_warn("tt", "memory exceeded on root lookup");
        }

        ret = create_state(hash);
        memcpy(ret->p, b->p, TOTAL_BOARD_SIZ);
        ret->last_eaten = b->last_eaten;
        ret->last_passed = (b->last_played == PASS);
        omp_set_lock(&ret->lock);

        if(is_black)
        {
            ret->next = b_stats_table[key];
            b_stats_table[key] = ret;
        }
        else
        {
            ret->next = w_stats_table[key];
            w_stats_table[key] = ret;
        }
        omp_unset_lock(bucket_lock);
    }
    else /* update */
    {
        omp_set_lock(&ret->lock);
        omp_unset_lock(bucket_lock);
    }

    return ret;
}

/*
Looks up a previously stored state, or generates a new one. No assumptions are
made about whether the board state is in reduced form already. If the limit on
states has been met the function returns NULL. If the state is found and
returned it's OpenMP lock is first set. Thread-safe.
RETURNS the state information or NULL
*/
tt_stats * transpositions_lookup_null(
    const cfg_board * cb,
    bool is_black,
    u64 hash
){
    u32 key = (u32)(hash % ((u64)number_of_buckets));
    omp_lock_t * bucket_lock = is_black ? &b_locks_table[key] :
        &w_locks_table[key];
    omp_set_lock(bucket_lock);

    tt_stats * ret = find_state2(hash, cb, is_black);
    if(ret == NULL) /* doesnt exist */
    {
        if(states_in_use >= max_allocated_states)
        {
            omp_unset_lock(bucket_lock);
            return NULL;
        }

        ret = create_state(hash);
        memcpy(ret->p, cb->p, TOTAL_BOARD_SIZ);
        ret->last_eaten = cb->last_eaten;
        ret->last_passed = (cb->last_played == PASS);
        omp_set_lock(&ret->lock);

        if(is_black)
        {
            ret->next = b_stats_table[key];
            b_stats_table[key] = ret;
        }
        else
        {
            ret->next = w_stats_table[key];
            w_stats_table[key] = ret;
        }
        omp_unset_lock(bucket_lock);
    }
    else /* update */
    {
        omp_set_lock(&ret->lock);
        omp_unset_lock(bucket_lock);
    }

    return ret;
}

/*
Frees all game states and resets counters.
*/
u32 tt_clean_all()
{
    u32 states_in_use_before = states_in_use;
    maintenance_mark = 0;

    for(u32 i = 0; i < number_of_buckets; ++i)
    {
        /* black table */
        while(b_stats_table[i] != NULL)
        {
            tt_stats * tmp = b_stats_table[i]->next;
            release_state(b_stats_table[i]);
            b_stats_table[i] = tmp;
        }
        /* white table */
        while(w_stats_table[i] != NULL)
        {
            tt_stats * tmp = w_stats_table[i]->next;
            release_state(w_stats_table[i]);
            w_stats_table[i] = tmp;
        }
    }

    d32 states_released = states_in_use_before - states_in_use;
    assert(states_released >= 0);
    return states_released;
}

/*
Mostly for debugging -- log the current memory status of the transpositions
table to stderr and log file.
*/
void transpositions_log_status()
{
    char * buf = alloc();
    u32 idx = snprintf(buf, MAX_PAGE_SIZ,
        "\n*** Transpositions table trace start ***\n\n");
    idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "Max size in MiB: %" PRIu64
      "\n", max_size_in_mbs);
    idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "Max allocated states: %u\n",
        max_allocated_states);
    idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "Allocated states: %u\n",
        allocated_states);
    idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "States in use: %u\n",
        states_in_use);
    idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "Number of buckets: %u\n",
        number_of_buckets);
    snprintf(buf + idx, MAX_PAGE_SIZ - idx, "Maintenance mark: %u\n",
        maintenance_mark);

    flog_warn("tt", buf);
    release(buf);
}

