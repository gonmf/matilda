/*
A cfg_board structure is a common fate graph board representation that is used
for fast atari checking; because of this it is useful specially in heavy
playouts.

A cfg_board is built from a previous board structure, but the two are not
linked; i.e. changes in one don't reflect in the other.

Building and destroying (freeing) a cfg_board are costly operations that should
be used only if the cfg_board will be used in playing many turns. cfg_board
structures are partially dynamically created and as such cannot be simply
memcpied to reuse the same starting game point. Undo is also not supported.

Freed cfg_board information is kept in cache for fast access in the future; it
is best to first free previous instances before creating new ones, thus limiting
the size the cache has to have.

Just like in the rest of the source code, all functions are not thread safe
unless explicitly said so.
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <omp.h>

#include "alloc.h"
#include "board.h"
#include "cfg_board.h"
#include "flog.h"
#include "move.h"
#include "types.h"
#include "zobrist.h"

/* from board_constants */
extern u8 out_neighbors8[TOTAL_BOARD_SIZ];
extern u8 out_neighbors4[TOTAL_BOARD_SIZ];
extern move_seq neighbors_side[TOTAL_BOARD_SIZ];
extern move_seq neighbors_diag[TOTAL_BOARD_SIZ];
extern move_seq neighbors_3x3[TOTAL_BOARD_SIZ];
extern bool border_left[TOTAL_BOARD_SIZ];
extern bool border_right[TOTAL_BOARD_SIZ];
extern bool border_top[TOTAL_BOARD_SIZ];
extern bool border_bottom[TOTAL_BOARD_SIZ];
extern u8 active_bits_in_byte[256];

/* from zobrist */
extern u16 iv_3x3[TOTAL_BOARD_SIZ][TOTAL_BOARD_SIZ][3];
extern u16 initial_3x3_hash[TOTAL_BOARD_SIZ];

static group * saved_nodes[MAXIMUM_NUM_THREADS];

static group * alloc_group() {
    int thread = omp_get_thread_num();
    group * ret;

    if (saved_nodes[thread] != NULL) {
        ret = saved_nodes[thread];
        saved_nodes[thread] = saved_nodes[thread]->next;
    } else {
        ret = malloc(sizeof(group));

        if (ret == NULL) {
            flog_crit("cfg", "system out of memory");
        }
    }

    return ret;
}

static void just_delloc_group(
    group * g
) {
    int thread = omp_get_thread_num();
    g->next = saved_nodes[thread];
    saved_nodes[thread] = g;
}

static void delloc_group(
    cfg_board * cb,
    group * g
) {
    cb->unique_groups_count--;

    if (g->unique_groups_idx < cb->unique_groups_count) {
        cb->unique_groups[g->unique_groups_idx] = cb->unique_groups[cb->unique_groups_count];
        cb->g[cb->unique_groups[g->unique_groups_idx]]->unique_groups_idx = g->unique_groups_idx;
    }

    int thread = omp_get_thread_num();
    g->next = saved_nodes[thread];
    saved_nodes[thread] = g;
}

static void pos_set_occupied(
    cfg_board * cb,
    bool is_black,
    move m
) {
    assert(neighbors_side[m].count < 5);
    assert(neighbors_diag[m].count < 5);
    assert(cb->p[m] > 0);

    u8 idx = cb->p[m] - 1;
    if (is_black) {
        for (u8 k = 0; k < neighbors_side[m].count; ++k) {
            move n = neighbors_side[m].coord[k];
            cb->black_neighbors4[n]++;
            cb->black_neighbors8[n]++;

            cb->hash[n] += iv_3x3[n][m][idx];
        }

        for (u8 k = 0; k < neighbors_diag[m].count; ++k) {
            move n = neighbors_diag[m].coord[k];
            cb->black_neighbors8[n]++;

            cb->hash[n] += iv_3x3[n][m][idx];
        }
    } else {
        for (u8 k = 0; k < neighbors_side[m].count; ++k) {
            move n = neighbors_side[m].coord[k];
            cb->white_neighbors4[n]++;
            cb->white_neighbors8[n]++;

            cb->hash[n] += iv_3x3[n][m][idx];
        }

        for (u8 k = 0; k < neighbors_diag[m].count; ++k) {
            move n = neighbors_diag[m].coord[k];
            cb->white_neighbors8[n]++;

            cb->hash[n] += iv_3x3[n][m][idx];
        }
    }
}

static void pos_set_free(
    cfg_board * cb,
    move m,
    bool is_black
) {
    assert(neighbors_side[m].count < 5);
    assert(neighbors_diag[m].count < 5);
    assert(cb->p[m] > 0);

    if (is_black) {
        for (u8 k = 0; k < neighbors_side[m].count; ++k) {
            move n = neighbors_side[m].coord[k];
            cb->black_neighbors4[n]--;
            cb->black_neighbors8[n]--;

            cb->hash[n] ^= iv_3x3[n][m][cb->p[m] - 1];
        }

        for (u8 k = 0; k < neighbors_diag[m].count; ++k) {
            move n = neighbors_diag[m].coord[k];
            cb->black_neighbors8[n]--;

            cb->hash[n] ^= iv_3x3[n][m][cb->p[m] - 1];
        }
    } else {
        for (u8 k = 0; k < neighbors_side[m].count; ++k) {
            move n = neighbors_side[m].coord[k];
            cb->white_neighbors4[n]--;
            cb->white_neighbors8[n]--;

            cb->hash[n] ^= iv_3x3[n][m][cb->p[m] - 1];
        }

        for (u8 k = 0; k < neighbors_diag[m].count; ++k) {
            move n = neighbors_diag[m].coord[k];
            cb->white_neighbors8[n]--;

            cb->hash[n] ^= iv_3x3[n][m][cb->p[m] - 1];
        }
    }
}

static void add_neighbor(
    group * restrict g,
    group * restrict n
) {
    for (u8 i = 0; i < g->neighbors_count; ++i) {
        if (g->neighbors[i] == n->stones.coord[0]) {
            return;
        }
    }

    g->neighbors[g->neighbors_count++] = n->stones.coord[0];
    n->neighbors[n->neighbors_count++] = g->stones.coord[0];
}

static void add_liberty(
    group * g,
    move m
) {
    u8 mask = (1 << (m % 8));

    if ((g->ls[m / 8] & mask) == 0) {
        g->ls[m / 8] |= mask;
        g->liberties++;

        if (m < g->liberties_min_coord) {
            g->liberties_min_coord = m;
        }
    }
}

static void add_liberty_unchecked(
    group * g,
    move m
) {
    u8 mask = (1 << (m % 8));
    g->ls[m / 8] |= mask;
    g->liberties++;

    if (m < g->liberties_min_coord) {
        g->liberties_min_coord = m;
    }
}

static void rem_liberty_unchecked(
    group * g,
    move m
) {
    u8 mask = (1 << (m % 8));
    g->ls[m / 8] &= ~mask;
    g->liberties--;
}

static void rem_neighbor(
    group * restrict g,
    const group * restrict to_remove
) {
    for (u8 j = 0; j < g->neighbors_count; ++j) {
        if (g->neighbors[j] == to_remove->stones.coord[0]) {
            g->neighbors[j] = g->neighbors[g->neighbors_count - 1];
            g->neighbors_count--;
            return;
        }
    }

    flog_crit("cfg", "CFG group neighbor not found");
}

static void unite_groups(
    cfg_board * cb,
    group * restrict to_keep,
    group * restrict to_replace
) {
    assert(to_keep != to_replace);
    assert(to_keep->is_black == to_replace->is_black);

    copy_moves(&to_keep->stones, &to_replace->stones);

    for (move i = 0; i < to_replace->stones.count; ++i) {
        move m = to_replace->stones.coord[i];
        assert(cb->g[m] == to_replace);
        cb->g[m] = to_keep;
    }

    for (u8 i = 0; i < to_replace->neighbors_count; ++i) {
        add_neighbor(to_keep, cb->g[to_replace->neighbors[i]]);
        rem_neighbor(cb->g[to_replace->neighbors[i]], to_replace);
    }

    if (to_replace->liberties == 0) {
        delloc_group(cb, to_replace);
        return;
    }

    u8 new_lib_count = 0;
    for (u8 i = 0; i < LIB_BITMAP_SIZ; ++i) {
        to_keep->ls[i] |= to_replace->ls[i];
        new_lib_count += active_bits_in_byte[to_keep->ls[i]];
    }
    to_keep->liberties = new_lib_count;

    if (to_replace->liberties_min_coord < to_keep->liberties_min_coord)
        to_keep->liberties_min_coord = to_replace->liberties_min_coord;
    delloc_group(cb, to_replace);
}

/*
Adds a stone to the group information of a struct cfg_board.
Doesn't capture anything.
*/
static void add_stone(
    cfg_board * cb,
    bool is_black,
    move m
) {
    /* Create new stone group */
    assert(cb->g[m] == NULL);

    cb->g[m] = alloc_group();
    cb->g[m]->is_black = is_black;
    cb->g[m]->liberties = 0;
    memset(cb->g[m]->ls, 0, LIB_BITMAP_SIZ);
    cb->g[m]->liberties_min_coord = TOTAL_BOARD_SIZ;
    cb->g[m]->neighbors_count = 0;
    cb->g[m]->stones.count = 1;
    cb->g[m]->stones.coord[0] = m;

    cb->unique_groups[cb->unique_groups_count] = m;
    cb->g[m]->unique_groups_idx = cb->unique_groups_count;
    cb->unique_groups_count++;

    /* Update neighbor stone counts */
    pos_set_occupied(cb, is_black, m);

    if (cb->black_neighbors4[m] + cb->white_neighbors4[m] == 0) {
        if (!border_left[m]) {
            add_liberty(cb->g[m], m + LEFT);
        }
        if (!border_right[m]) {
            add_liberty(cb->g[m], m + RIGHT);
        }
        if (!border_top[m]) {
            add_liberty(cb->g[m], m + TOP);
        }
        if (!border_bottom[m]) {
            add_liberty(cb->g[m], m + BOTTOM);
        }
        return;
    }

    group * n;
    group * neighbors[4];
    u8 neighbors_n = 0;

    if (!border_left[m]) {
        if ((n = cb->g[m + LEFT]) == NULL) {
            add_liberty(cb->g[m], m + LEFT);
        } else {
            neighbors[neighbors_n++] = n;
            rem_liberty_unchecked(n, m);

            if (n->is_black == is_black) {
                unite_groups(cb, n, cb->g[m]);
            } else {
                add_neighbor(cb->g[m], n);
            }
        }
    }

    if (!border_right[m]) {
        if ((n = cb->g[m + RIGHT]) == NULL) {
            add_liberty(cb->g[m], m + RIGHT);
        } else {
            bool found = false;
            for (u8 i = 0; i < neighbors_n; ++i) {
                if (neighbors[i] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                neighbors[neighbors_n++] = n;
                rem_liberty_unchecked(n, m);

                if (n->is_black == is_black) {
                    unite_groups(cb, n, cb->g[m]);
                } else {
                    add_neighbor(cb->g[m], n);
                }
            }
        }
    }

    if (!border_top[m]) {
        if ((n = cb->g[m + TOP]) == NULL) {
            add_liberty(cb->g[m], m + TOP);
        } else {
            bool found = false;
            for (u8 i = 0; i < neighbors_n; ++i) {
                if (neighbors[i] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                neighbors[neighbors_n++] = n;
                rem_liberty_unchecked(n, m);

                if (n->is_black == is_black) {
                    unite_groups(cb, n, cb->g[m]);
                } else {
                    add_neighbor(cb->g[m], n);
                }
            }
        }
    }

    if (!border_bottom[m]) {
        if ((n = cb->g[m + BOTTOM]) == NULL) {
            add_liberty(cb->g[m], m + BOTTOM);
        } else {
            bool found = false;
            for (u8 i = 0; i < neighbors_n; ++i) {
                if (neighbors[i] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                neighbors[neighbors_n++] = n;
                rem_liberty_unchecked(n, m);

                if (n->is_black == is_black) {
                    unite_groups(cb, n, cb->g[m]);
                } else {
                    add_neighbor(cb->g[m], n);
                }
            }
        }
    }
}

/*
Tests if the two structures have the same board contents.
RETURNS true if the structures are equal in board contents
*/
bool cfg_board_are_equal(
    cfg_board * restrict a,
    const board * restrict b
) {
    return memcmp(a->p, b->p, TOTAL_BOARD_SIZ) == 0 && a->last_played == b->last_played &&
        a->last_eaten == b->last_eaten;
}

/*
Initiliazes the data pointed to cb, to hold a valid (but empty) board.
*/
void cfg_init_board(
    cfg_board * cb
) {
    memset(cb->p, EMPTY, TOTAL_BOARD_SIZ);
    cb->last_played = cb->last_eaten = NONE;

    memcpy(cb->hash, initial_3x3_hash, TOTAL_BOARD_SIZ * sizeof(u16));
    memset(cb->black_neighbors4, 0, TOTAL_BOARD_SIZ);
    memset(cb->white_neighbors4, 0, TOTAL_BOARD_SIZ);
    memset(cb->black_neighbors8, 0, TOTAL_BOARD_SIZ);
    memset(cb->white_neighbors8, 0, TOTAL_BOARD_SIZ);
    memset(cb->g, 0, TOTAL_BOARD_SIZ * sizeof(group *));
    cb->empty.count = 0;
    cb->unique_groups_count = 0;

    for (move m = 0; m < TOTAL_BOARD_SIZ; ++m) {
        cb->empty.coord[cb->empty.count] = m;
        cb->empty.count++;
    }

    assert(verify_cfg_board(cb));
}

/*
Converts a board structure into an cfg_board structure; the two are not linked;
changing one will not modify the other.
*/
void cfg_from_board(
    cfg_board * restrict dst,
    const board * restrict src
) {
    memcpy(dst, src, sizeof(board));
    memcpy(dst->hash, initial_3x3_hash, TOTAL_BOARD_SIZ * sizeof(u16));
    memset(dst->black_neighbors4, 0, TOTAL_BOARD_SIZ);
    memset(dst->white_neighbors4, 0, TOTAL_BOARD_SIZ);
    memset(dst->black_neighbors8, 0, TOTAL_BOARD_SIZ);
    memset(dst->white_neighbors8, 0, TOTAL_BOARD_SIZ);
    memset(dst->g, 0, TOTAL_BOARD_SIZ * sizeof(group *));
    dst->empty.count = 0;
    dst->unique_groups_count = 0;

    for (move m = 0; m < TOTAL_BOARD_SIZ; ++m) {
        if (src->p[m] == EMPTY) {
            dst->empty.coord[dst->empty.count] = m;
            dst->empty.count++;
        } else {
            add_stone(dst, src->p[m] == BLACK_STONE, m);
        }
    }

    assert(cfg_board_are_equal(dst, src));
    assert(verify_cfg_board(dst));
}

/*
Clones a CFG board into another, independent, instance.
*/
void cfg_board_clone(
    cfg_board * restrict dst,
    const cfg_board * restrict src
) {
    /* copy most of the structure */
    memcpy(dst, src, sizeof(cfg_board) - (TOTAL_BOARD_SIZ * sizeof(group *)));
    memset(dst->g, 0, TOTAL_BOARD_SIZ * sizeof(group *));

    for (u8 i = 0; i < src->unique_groups_count; ++i) {
        /* copy group information */
        group * g = alloc_group();
        group * s = src->g[src->unique_groups[i]];
        assert(s->unique_groups_idx == i);
        memcpy(g, s, ((char *)&s->neighbors[s->neighbors_count]) - ((char *)s));

        /* replace hard links to group information */
        for (move j = 0; j < g->stones.count; ++j) {
            move m = g->stones.coord[j];
            dst->g[m] = g;
        }
    }

    assert(verify_cfg_board(dst));
}


static void add_liberties_to_neighbors(
    cfg_board * cb,
    move m,
    u8 own
) {
    if (!border_left[m] && cb->p[m + LEFT] == own) {
        add_liberty(cb->g[m + LEFT], m);
    }

    if (!border_right[m] && cb->p[m + RIGHT] == own) {
        add_liberty(cb->g[m + RIGHT], m);
    }

    if (!border_top[m] && cb->p[m + TOP] == own) {
        add_liberty(cb->g[m + TOP], m);
    }

    if (!border_bottom[m] && cb->p[m + BOTTOM] == own) {
        add_liberty(cb->g[m + BOTTOM], m);
    }
}

static void cfg_board_kill_group(
    cfg_board * cb,
    group * g,
    u8 own
) {
    move id = g->stones.coord[0];

    for (move i = 0; i < g->stones.count; ++i) {
        move m = g->stones.coord[i];

        pos_set_free(cb, m, g->is_black);
        cb->p[m] = EMPTY;
        cb->g[m] = NULL;
        add_liberties_to_neighbors(cb, m, own);

        cb->empty.coord[cb->empty.count] = m;
        cb->empty.count++;
    }

    for (u8 i = 0; i < g->neighbors_count; ++i) {
        group * nei = cb->g[g->neighbors[i]];

        for (u8 j = 0; j < nei->neighbors_count; ++j) {
            if (nei->neighbors[j] == id) {
                nei->neighbors_count--;
                nei->neighbors[j] = nei->neighbors[nei->neighbors_count];
                break;
            }
        }
    }

    delloc_group(cb, g);
}

static void cfg_board_kill_group2(
    cfg_board * cb,
    group * g,
    u8 own,
    u64 * zobrist_hash
) {
    move id = g->stones.coord[0];

    for (move i = 0; i < g->stones.count; ++i) {
        move m = g->stones.coord[i];

        zobrist_update_hash(zobrist_hash, m, cb->p[m]);
        pos_set_free(cb, m, g->is_black);
        cb->p[m] = EMPTY;
        cb->g[m] = NULL;
        add_liberties_to_neighbors(cb, m, own);

        cb->empty.coord[cb->empty.count] = m;
        cb->empty.count++;
    }

    for (u8 i = 0; i < g->neighbors_count; ++i) {
        group * nei = cb->g[g->neighbors[i]];

        for (u8 j = 0; j < nei->neighbors_count; ++j) {
            if (nei->neighbors[j] == id) {
                nei->neighbors_count--;
                nei->neighbors[j] = nei->neighbors[nei->neighbors_count];
                break;
            }
        }
    }

    delloc_group(cb, g);
}

static void cfg_board_kill_group3(
    cfg_board * cb,
    group * g,
    u8 own,
    bool stones_removed[static TOTAL_BOARD_SIZ],
    u8 rem_nei_libs[static LIB_BITMAP_SIZ]
) {
    move id = g->stones.coord[0];

    for (move i = 0; i < g->stones.count; ++i) {
        move m = g->stones.coord[i];
        assert(cb->p[m] != EMPTY);
        pos_set_free(cb, m, g->is_black);
        cb->p[m] = EMPTY;
        cb->g[m] = NULL;
        stones_removed[m] = true;
        add_liberties_to_neighbors(cb, m, own);

        cb->empty.coord[cb->empty.count] = m;
        cb->empty.count++;
    }

    for (u8 i = 0; i < g->neighbors_count; ++i) {
        group * nei = cb->g[g->neighbors[i]];

        for (u8 i = 0; i < LIB_BITMAP_SIZ; ++i) {
            rem_nei_libs[i] |= nei->ls[i];
        }

        for (u8 j = 0; j < nei->neighbors_count; ++j) {
            if (nei->neighbors[j] == id) {
                nei->neighbors_count--;
                nei->neighbors[j] = nei->neighbors[nei->neighbors_count];
                break;
            }
        }
    }

    delloc_group(cb, g);
}

/*
Apply a passing turn.
*/
void just_pass(
    cfg_board * cb
) {
    cb->last_played = PASS;
    cb->last_eaten = NONE;
}

/*
Assume play is legal and update the structure, capturing
accordingly.
*/
void just_play(
    cfg_board * cb,
    bool is_black,
    move m
) {
    assert(verify_cfg_board(cb));
    assert(is_board_move(m));
    assert(cb->p[m] == EMPTY);
    assert(cb->g[m] == NULL);

    u8 own = is_black ? BLACK_STONE : WHITE_STONE;

    move captures = 0;
    move one_stone_captured = NONE;
    group * n;

    cb->p[m] = own;
    add_stone(cb, is_black, m);

    u8 n8 = is_black ? cb->white_neighbors4[m] : cb->black_neighbors4[m];
    if (n8 > 0) {
        if (!border_left[m]) {
            n = cb->g[m + LEFT];

            if (n != NULL && n->is_black != is_black && n->liberties == 0) {
                captures += n->stones.count;
                cfg_board_kill_group(cb, n, own);
                one_stone_captured = m + LEFT;
            }
        }

        if (!border_right[m]) {
            n = cb->g[m + RIGHT];

            if (n != NULL && n->is_black != is_black && n->liberties == 0) {
                captures += n->stones.count;
                cfg_board_kill_group(cb, n, own);
                one_stone_captured = m + RIGHT;
            }
        }

        if (!border_top[m]) {
            n = cb->g[m + TOP];

            if (n != NULL && n->is_black != is_black && n->liberties == 0) {
                captures += n->stones.count;
                cfg_board_kill_group(cb, n, own);
                one_stone_captured = m + TOP;
            }
        }

        if (!border_bottom[m]) {
            n = cb->g[m + BOTTOM];

            if (n != NULL && n->is_black != is_black && n->liberties == 0) {
                captures += n->stones.count;
                cfg_board_kill_group(cb, n, own);
                one_stone_captured = m + BOTTOM;
            }
        }
    }

    if (captures == 1) {
        cb->last_eaten = one_stone_captured;
    } else {
        cb->last_eaten = NONE;
    }

    cb->last_played = m;

    /* Remove position from list of empty intersections */
    for (move k = 0; k < cb->empty.count; ++k) {
        if (cb->empty.coord[k] == m) {
            cb->empty.count--;
            cb->empty.coord[k] = cb->empty.coord[cb->empty.count];
            break;
        }
    }
}

/*
Assume play is legal and update the structure, capturing
accordingly.
Also updates a Zobrist hash value.
*/
void just_play2(
    cfg_board * cb,
    bool is_black,
    move m,
    u64 * zobrist_hash
) {
    assert(verify_cfg_board(cb));
    assert(is_board_move(m));
    assert(cb->p[m] == EMPTY);
    assert(cb->g[m] == NULL);

    u8 own = is_black ? BLACK_STONE : WHITE_STONE;

    move captures = 0;
    move one_stone_captured = NONE;
    group * n;

    cb->p[m] = own;
    add_stone(cb, is_black, m);
    zobrist_update_hash(zobrist_hash, m, own);

    u8 n8 = is_black ? cb->white_neighbors4[m] : cb->black_neighbors4[m];
    if (n8 > 0) {
        if (!border_left[m]) {
            n = cb->g[m + LEFT];

            if (n != NULL && n->is_black != is_black && n->liberties == 0) {
                captures += n->stones.count;
                cfg_board_kill_group2(cb, n, own, zobrist_hash);
                one_stone_captured = m + LEFT;
            }
        }

        if (!border_right[m]) {
            n = cb->g[m + RIGHT];

            if (n != NULL && n->is_black != is_black && n->liberties == 0) {
                captures += n->stones.count;
                cfg_board_kill_group2(cb, n, own, zobrist_hash);
                one_stone_captured = m + RIGHT;
            }
        }

        if (!border_top[m]) {
            n = cb->g[m + TOP];

            if (n != NULL && n->is_black != is_black && n->liberties == 0) {
                captures += n->stones.count;
                cfg_board_kill_group2(cb, n, own, zobrist_hash);
                one_stone_captured = m + TOP;
            }
        }

        if (!border_bottom[m]) {
            n = cb->g[m + BOTTOM];

            if (n != NULL && n->is_black != is_black && n->liberties == 0) {
                captures += n->stones.count;
                cfg_board_kill_group2(cb, n, own, zobrist_hash);
                one_stone_captured = m + BOTTOM;
            }
        }
    }

    if (captures == 1) {
        cb->last_eaten = one_stone_captured;
    } else {
        cb->last_eaten = NONE;
    }

    cb->last_played = m;

    /* Remove position from list of empty intersections */
    for (move k = 0; k < cb->empty.count; ++k) {
        if (cb->empty.coord[k] == m) {
            cb->empty.count--;
            cb->empty.coord[k] = cb->empty.coord[cb->empty.count];
            break;
        }
    }
}

/*
Assume play is legal and update the structure, capturing accordingly. Also
updates a stone difference and fills a matrix of captured stones and a bitmap of
liberties of neighbors of the captured groups. Does NOT clear the matrix and
bitmap.
*/
void just_play3(
    cfg_board * cb,
    bool is_black,
    move m,
    d16 * stone_difference,
    bool stones_removed[static TOTAL_BOARD_SIZ],
    u8 rem_nei_libs[static LIB_BITMAP_SIZ]
) {
    assert(verify_cfg_board(cb));
    assert(is_board_move(m));
    assert(cb->p[m] == EMPTY);
    assert(cb->g[m] == NULL);

    u8 own = is_black ? BLACK_STONE : WHITE_STONE;

    move captures = 0;
    move one_stone_captured = NONE;
    group * n;

    cb->p[m] = own;
    add_stone(cb, is_black, m);

    u8 n8 = is_black ? cb->white_neighbors4[m] : cb->black_neighbors4[m];
    if (n8 > 0) {
        if (!border_left[m]) {
            n = cb->g[m + LEFT];

            if (n != NULL && n->is_black != is_black && n->liberties == 0) {
                captures += n->stones.count;
                cfg_board_kill_group3(cb, n, own, stones_removed, rem_nei_libs);
                one_stone_captured = m + LEFT;
            }
        }

        if (!border_right[m]) {
            n = cb->g[m + RIGHT];

            if (n != NULL && n->is_black != is_black && n->liberties == 0) {
                captures += n->stones.count;
                cfg_board_kill_group3(cb, n, own, stones_removed, rem_nei_libs);
                one_stone_captured = m + RIGHT;
            }
        }

        if (!border_top[m]) {
            n = cb->g[m + TOP];

            if (n != NULL && n->is_black != is_black && n->liberties == 0) {
                captures += n->stones.count;
                cfg_board_kill_group3(cb, n, own, stones_removed, rem_nei_libs);
                one_stone_captured = m + TOP;
            }
        }

        if (!border_bottom[m]) {
            n = cb->g[m + BOTTOM];

            if (n != NULL && n->is_black != is_black && n->liberties == 0) {
                captures += n->stones.count;
                cfg_board_kill_group3(cb, n, own, stones_removed, rem_nei_libs);
                one_stone_captured = m + BOTTOM;
            }
        }
    }

    if (captures == 1) {
        cb->last_eaten = one_stone_captured;
    } else {
        cb->last_eaten = NONE;
    }

    cb->last_played = m;

    d16 stone_diff = 1 + captures;
    *stone_difference += is_black ? stone_diff : -stone_diff;

    /* Remove position from list of empty intersections */
    for (move k = 0; k < cb->empty.count; ++k) {
        if (cb->empty.coord[k] == m) {
            cb->empty.count--;
            cb->empty.coord[k] = cb->empty.coord[cb->empty.count];
            break;
        }
    }

    assert(verify_cfg_board(cb));
}

static void add_group_liberties(
    group * restrict dst,
    const group * restrict src
) {
    u8 new_lib_count = 0;

    for (u8 i = 0; i < LIB_BITMAP_SIZ; ++i) {
        dst->ls[i] |= src->ls[i];
        new_lib_count += active_bits_in_byte[dst->ls[i]];
    }

    dst->liberties = new_lib_count;
}

static bool are_neighbors(
    const cfg_board * cb,
    group * g,
    group ** neighbors,
    u8 neighbors_n
) {
    for (u8 i = 0; i < g->neighbors_count; ++i) {
        group * nei = cb->g[g->neighbors[i]];

        for (u8 k = 0; k < neighbors_n; ++k) {
            if (neighbors[k] == nei) {
                return true;
            }
        }
    }

    return false;
}

static void cfg_board_give_neighbors_libs(
    cfg_board * cb,
    group * g,
    group ** neighbors,
    u8 neighbors_n
) {
    for (move i = 0; i < g->stones.count; ++i) {
        move m = g->stones.coord[i];

        for (u8 k = 0; k < neighbors_n; ++k) {
            if (!border_left[m] && cb->g[m + LEFT] == neighbors[k]) {
                add_liberty(cb->g[m + LEFT], m);
            }

            if (!border_right[m] && cb->g[m + RIGHT] == neighbors[k]) {
                add_liberty(cb->g[m + RIGHT], m);
            }

            if (!border_top[m] && cb->g[m + TOP] == neighbors[k]) {
                add_liberty(cb->g[m + TOP], m);
            }

            if (!border_bottom[m] && cb->g[m + BOTTOM] == neighbors[k]) {
                add_liberty(cb->g[m + BOTTOM], m);
            }
        }
    }
}


/*
Detects one stone ko rule violations.
Doesn't test other types of legality.
RETURNS true if play is illegal due to ko
*/
bool ko_violation(
    const cfg_board * cb,
    move m
) {
    assert(verify_cfg_board(cb));
    assert(is_board_move(m));

    return cb->last_eaten == m && cb->g[cb->last_played]->stones.count == 1 &&
        cb->g[cb->last_played]->liberties == 1;
}

/*
If ko is possible, returns the offending play.
RETURNS position in ko, or NONE
*/
move get_ko_play(
    const cfg_board * cb
) {
    if (is_board_move(cb->last_eaten) && cb->g[cb->last_played]->stones.count == 1 &&
        cb->g[cb->last_played]->liberties == 1) {
        return cb->last_eaten;
    }

    return NONE;
}

/*
Calculates the liberties after playing and the number of stones captured.
Does not test ko.
RETURNS number of liberties after play
*/
u8 libs_after_play(
    cfg_board * cb,
    bool is_black,
    move m,
    move * caps
) {
    assert(verify_cfg_board(cb));
    assert(cb->p[m] == EMPTY);
    assert(cb->g[m] == NULL);

    if (cb->black_neighbors4[m] + cb->white_neighbors4[m] == 0) {
        *caps = false;
        return 4 - out_neighbors4[m];
    }

    group * n;
    /* warning: some fields are not initialized because they're not used */
    group g;
    g.liberties = 0;
    g.liberties_min_coord = TOTAL_BOARD_SIZ;
    memset(g.ls, 0, LIB_BITMAP_SIZ);
    add_liberty_unchecked(&g, m);

    /* list of same color neighbors */
    group * neighbors[4];
    u8 neighbors_n = 0;

    if (!border_left[m]) {
        n = cb->g[m + LEFT];
        if (n == NULL) {
            add_liberty_unchecked(&g, m + LEFT);
        } else if (n->is_black == is_black) {
            neighbors[neighbors_n++] = n;
        }
    }

    if (!border_right[m]) {
        n = cb->g[m + RIGHT];
        if (n == NULL) {
            add_liberty_unchecked(&g, m + RIGHT);
        } else if (n->is_black == is_black) {
            bool found = false;
            for (u8 k = 0; k < neighbors_n; ++k) {
                if (neighbors[k] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                neighbors[neighbors_n++] = n;
            }
        }
    }

    if (!border_top[m]) {
        n = cb->g[m + TOP];
        if (n == NULL) {
            add_liberty_unchecked(&g, m + TOP);
        } else if (n->is_black == is_black) {
            bool found = false;
            for (u8 k = 0; k < neighbors_n; ++k)
                if (neighbors[k] == n) {
                    found = true;
                    break;
                }

            if (!found) {
                neighbors[neighbors_n++] = n;
            }
        }
    }

    if (!border_bottom[m]) {
        n = cb->g[m + BOTTOM];
        if (n == NULL) {
            add_liberty_unchecked(&g, m + BOTTOM);
        } else if (n->is_black == is_black) {
            bool found = false;
            for (u8 k = 0; k < neighbors_n; ++k)
                if (neighbors[k] == n) {
                    found = true;
                    break;
                }

            if (!found) {
                neighbors[neighbors_n++] = n;
            }
        }
    }

    u8 on4 = is_black ? cb->white_neighbors4[m] : cb->black_neighbors4[m];
    if (on4 == 0) {
        *caps = false;
        for (u8 k = 0; k < neighbors_n; ++k) {
            add_group_liberties(&g, neighbors[k]);
        }

        return g.liberties - 1;
    }

    /*
    Backup neighbor groups before being modified
    */
    u8 neighbor_bak_ls[4][LIB_BITMAP_SIZ];
    u8 neighbor_bak_libs[4];
    for (u8 k = 0; k < neighbors_n; ++k) {
        memcpy(neighbor_bak_ls[k], neighbors[k]->ls, LIB_BITMAP_SIZ);
        neighbor_bak_libs[k] = neighbors[k]->liberties;
    }


    /*
    First capture enemy stones, saving them for later
    */
    move captured = 0;
    group * opt_neighbors[4];
    u8 opt_neighbors_n = 0;

    if (!border_left[m]) {
        n = cb->g[m + LEFT];

        if (n != NULL && n->is_black != is_black && n->liberties == 1) {
            add_liberty_unchecked(&g, m + LEFT);
            opt_neighbors[opt_neighbors_n++] = n;
            cfg_board_give_neighbors_libs(cb, n, neighbors, neighbors_n);
            captured += n->stones.count;
        }
    }

    if (!border_right[m]) {
        n = cb->g[m + RIGHT];

        if (n != NULL && n->is_black != is_black && n->liberties == 1) {
            add_liberty_unchecked(&g, m + RIGHT);

            bool found = false;
            for (u8 k = 0; k < opt_neighbors_n; ++k) {
                if (opt_neighbors[k] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                opt_neighbors[opt_neighbors_n++] = n;
                cfg_board_give_neighbors_libs(cb, n, neighbors, neighbors_n);
                captured += n->stones.count;
            }
        }
    }

    if (!border_top[m]) {
        n = cb->g[m + TOP];

        if (n != NULL && n->is_black != is_black && n->liberties == 1) {
            add_liberty_unchecked(&g, m + TOP);

            bool found = false;
            for (u8 k = 0; k < opt_neighbors_n; ++k) {
                if (opt_neighbors[k] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                opt_neighbors[opt_neighbors_n++] = n;
                cfg_board_give_neighbors_libs(cb, n, neighbors, neighbors_n);
                captured += n->stones.count;
            }
        }
    }

    if (!border_bottom[m]) {
        n = cb->g[m + BOTTOM];

        if (n != NULL && n->is_black != is_black && n->liberties == 1) {
            add_liberty_unchecked(&g, m + BOTTOM);

            bool found = false;
            for (u8 k = 0; k < opt_neighbors_n; ++k) {
                if (opt_neighbors[k] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                opt_neighbors[opt_neighbors_n++] = n;
                cfg_board_give_neighbors_libs(cb, n, neighbors, neighbors_n);
                captured += n->stones.count;
            }
        }
    }

    /*
    Now with updated liberty counts, count liberties and restore group liberty
    states
    */
    for (u8 k = 0; k < neighbors_n; ++k) {
        add_group_liberties(&g, neighbors[k]);
        memcpy(neighbors[k]->ls, neighbor_bak_ls[k], LIB_BITMAP_SIZ);
        neighbors[k]->liberties = neighbor_bak_libs[k];
    }

    *caps = captured;
    return g.liberties - 1;
}

/*
Calculates if playing at the designated position is legal and safe.
Also returns whether it would return in a capture.
Does not test ko.
RETURNS 0 for illegal, 1 for placed in atari, 2 for safe to play
*/
u8 safe_to_play2(
    cfg_board * cb,
    bool is_black,
    move m,
    bool * caps
) {
    assert(verify_cfg_board(cb));
    assert(cb->p[m] == EMPTY);
    assert(cb->g[m] == NULL);

    if (cb->white_neighbors8[m] + cb->black_neighbors8[m] + out_neighbors8[m] < 3) {
        *caps = false;
        return 2;
    }

    *caps = false;
    group * n;
    /* warning: some fields are not initialized because they're not used */
    group g;
    g.liberties = 0;
    g.liberties_min_coord = TOTAL_BOARD_SIZ;
    memset(g.ls, 0, LIB_BITMAP_SIZ);
    add_liberty_unchecked(&g, m);

    u8 probable_libs = 0;
    group * opt_neighbors[4];
    u8 opt_neighbors_n = 0;
    group * neighbors[4];
    u8 neighbors_n = 0;

    if (!border_left[m]) {
        n = cb->g[m + LEFT];

        if (n == NULL) {
            add_liberty_unchecked(&g, m + LEFT);
        } else if (n->is_black == is_black) {
            neighbors[neighbors_n++] = n;
            add_group_liberties(&g, n);
        } else if (n->liberties == 1) {
            add_liberty_unchecked(&g, m + LEFT);
            *caps = true;

            if (n->stones.count > 1) {
                opt_neighbors[opt_neighbors_n++] = n;
            }
        }
    }

    if (!border_right[m]) {
        n = cb->g[m + RIGHT];

        if (n == NULL) {
            add_liberty(&g, m + RIGHT);
        } else  if (n->is_black == is_black) {
            bool found = false;
            for (u8 k = 0; k < neighbors_n; ++k) {
                if (neighbors[k] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                neighbors[neighbors_n++] = n;
                add_group_liberties(&g, n);
            }
        } else if (n->liberties == 1) {
            add_liberty_unchecked(&g, m + RIGHT);
            *caps = true;

            if (n->stones.count > 1) {
                bool found = false;
                for (u8 k = 0; k < opt_neighbors_n; ++k) {
                    if (opt_neighbors[k] == n) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    opt_neighbors[opt_neighbors_n++] = n;
                }
            }
        }
    }

    if (!border_top[m]) {
        n = cb->g[m + TOP];

        if (n == NULL) {
            add_liberty(&g, m + TOP);
        } else if (n->is_black == is_black) {
            bool found = false;
            for (u8 k = 0; k < neighbors_n; ++k) {
                if (neighbors[k] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                neighbors[neighbors_n++] = n;
                add_group_liberties(&g, n);
            }
        } else if (n->liberties == 1) {
            add_liberty_unchecked(&g, m + TOP);
            *caps = true;

            if (n->stones.count > 1) {
                bool found = false;
                for (u8 k = 0; k < opt_neighbors_n; ++k) {
                    if (opt_neighbors[k] == n) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    opt_neighbors[opt_neighbors_n++] = n;
                }
            }
        }
    }

    if (!border_bottom[m]) {
        n = cb->g[m + BOTTOM];

        if (n == NULL) {
            add_liberty(&g, m + BOTTOM);
        } else if (n->is_black == is_black) {
            bool found = false;
            for (u8 k = 0; k < neighbors_n; ++k) {
                if (neighbors[k] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                neighbors[neighbors_n++] = n;
                add_group_liberties(&g, n);
            }
        } else if (n->liberties == 1) {
            add_liberty_unchecked(&g, m + BOTTOM);
            *caps = true;

            if (n->stones.count > 1) {
                bool found = false;
                for (u8 k = 0; k < opt_neighbors_n; ++k) {
                    if (opt_neighbors[k] == n) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    opt_neighbors[opt_neighbors_n++] = n;
                }
            }
        }
    }

    if (g.liberties > 2) {
        return 2;
    }

    for (u8 i = 0; i < opt_neighbors_n; ++i) {
        if (are_neighbors(cb, opt_neighbors[i], neighbors, neighbors_n)) {
            ++probable_libs;
        }
    }

    u8 libs = probable_libs + g.liberties - 1;
    return MIN(libs, 2);
}

/*
Calculates if playing at the designated position is legal and safe.
Does not test ko.
RETURNS 0 for illegal, 1 for placed in atari, 2 for safe to play
*/
u8 safe_to_play(
    cfg_board * cb,
    bool is_black,
    move m
) {
    assert(verify_cfg_board(cb));
    assert(cb->p[m] == EMPTY);
    assert(cb->g[m] == NULL);

    if (cb->white_neighbors4[m] + cb->black_neighbors4[m] + out_neighbors4[m] < 3) {
        return 2;
    }

    group * n;
    /* warning: some fields are not initialized because they're not used */
    group g;
    g.liberties = 0;
    g.liberties_min_coord = TOTAL_BOARD_SIZ;
    memset(g.ls, 0, LIB_BITMAP_SIZ);
    add_liberty_unchecked(&g, m);

    u8 probable_libs = 0;
    group * opt_neighbors[4];
    u8 opt_neighbors_n = 0;
    group * neighbors[4];
    u8 neighbors_n = 0;

    if (!border_left[m]) {
        n = cb->g[m + LEFT];

        if (n == NULL) {
            add_liberty_unchecked(&g, m + LEFT);
        } else if (n->is_black == is_black) {
            neighbors[neighbors_n++] = n;
            add_group_liberties(&g, n);
        } else if (n->liberties == 1) {
            add_liberty_unchecked(&g, m + LEFT);

            if (n->stones.count > 1) {
                opt_neighbors[opt_neighbors_n++] = n;
            }
        }
    }

    if (!border_right[m]) {
        n = cb->g[m + RIGHT];

        if (n == NULL) {
            add_liberty(&g, m + RIGHT);
        } else if (n->is_black == is_black) {
            bool found = false;
            for (u8 k = 0; k < neighbors_n; ++k) {
                if (neighbors[k] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                neighbors[neighbors_n++] = n;
                add_group_liberties(&g, n);
            }
        } else if (n->liberties == 1) {
            add_liberty_unchecked(&g, m + RIGHT);

            if (n->stones.count > 1) {
                bool found = false;
                for (u8 k = 0; k < opt_neighbors_n; ++k) {
                    if (opt_neighbors[k] == n) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    opt_neighbors[opt_neighbors_n++] = n;
                }
            }
        }
    }

    if (!border_top[m]) {
        n = cb->g[m + TOP];

        if (n == NULL) {
            add_liberty(&g, m + TOP);
        } else if (n->is_black == is_black) {
            bool found = false;
            for (u8 k = 0; k < neighbors_n; ++k) {
                if (neighbors[k] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                neighbors[neighbors_n++] = n;
                add_group_liberties(&g, n);
            }
        } else if (n->liberties == 1) {
            add_liberty_unchecked(&g, m + TOP);

            if (n->stones.count > 1) {
                bool found = false;
                for (u8 k = 0; k < opt_neighbors_n; ++k) {
                    if (opt_neighbors[k] == n) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    opt_neighbors[opt_neighbors_n++] = n;
                }
            }
        }
    }

    if (!border_bottom[m]) {
        n = cb->g[m + BOTTOM];

        if (n == NULL) {
            add_liberty(&g, m + BOTTOM);
        } else if (n->is_black == is_black) {
            bool found = false;
            for (u8 k = 0; k < neighbors_n; ++k) {
                if (neighbors[k] == n) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                neighbors[neighbors_n++] = n;
                add_group_liberties(&g, n);
            }
        } else if (n->liberties == 1) {
            add_liberty_unchecked(&g, m + BOTTOM);

            if (n->stones.count > 1) {
                bool found = false;
                for (u8 k = 0; k < opt_neighbors_n; ++k) {
                    if (opt_neighbors[k] == n) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    opt_neighbors[opt_neighbors_n++] = n;
                }
            }
        }
    }

    if (g.liberties > 2) {
        return 2;
    }

    for (u8 i = 0; i < opt_neighbors_n; ++i) {
        if (are_neighbors(cb, opt_neighbors[i], neighbors, neighbors_n)) {
            ++probable_libs;
        }
    }

    u8 libs = probable_libs + g.liberties - 1;
    return MIN(libs, 2);
}

/*
Tests if a play captures any opponent stone.
RETURNS true if any opponent stone is captured
*/
bool caps_after_play(
    const cfg_board * cb,
    bool is_black,
    move m
) {
    assert(verify_cfg_board(cb));
    assert(cb->p[m] == EMPTY);
    assert(cb->g[m] == NULL);

    u8 on4 = is_black ? cb->white_neighbors4[m] : cb->black_neighbors4[m];
    if (on4 == 0) {
        return false;
    }

    group * n;

    if (!border_left[m] && (n = cb->g[m + LEFT]) != NULL && n->is_black != is_black && n->liberties == 1) {
        return true;
    }
    if (!border_right[m] && (n = cb->g[m + RIGHT]) != NULL && n->is_black != is_black && n->liberties == 1) {
        return true;
    }
    if (!border_top[m] && (n = cb->g[m + TOP]) != NULL && n->is_black != is_black && n->liberties == 1) {
        return true;
    }
    if (!border_bottom[m] && (n = cb->g[m + BOTTOM]) != NULL && n->is_black != is_black && n->liberties == 1) {
        return true;
    }

    return false;
}

/*
RETURNS true if play is valid (validating ko rule)
*/
bool can_play(
    const cfg_board * cb,
    bool is_black,
    move m
) {
    assert(verify_cfg_board(cb));
    if (cb->p[m] != EMPTY) {
        return false;
    }

    if (cb->black_neighbors4[m] + cb->white_neighbors4[m] + out_neighbors4[m] < 4) {
        return true;
    }

    if (ko_violation(cb, m)) {
        return false;
    }

    assert(cb->g[m] == NULL);
    group * n;

    if (!border_left[m]) {
        n = cb->g[m + LEFT];

        if (n == NULL) {
            return true;
        } else if (n->is_black == is_black) {
            if (n->liberties != 1) {
                return true;
            }
        } else if (n->liberties == 1) {
            return true;
        }
    }

    if (!border_right[m]) {
        n = cb->g[m + RIGHT];

        if (n == NULL) {
            return true;
        } else if (n->is_black == is_black) {
            if (n->liberties != 1) {
                return true;
            }
        } else if (n->liberties == 1) {
            return true;
        }
    }

    if (!border_top[m]) {
        n = cb->g[m + TOP];

        if (n == NULL) {
            return true;
        } else if (n->is_black == is_black) {
            if (n->liberties != 1) {
                return true;
            }
        } else if (n->liberties == 1) {
            return true;
        }
    }

    if (!border_bottom[m]) {
        n = cb->g[m + BOTTOM];

        if (n == NULL) {
            return true;
        } else if (n->is_black == is_black) {
            if (n->liberties != 1) {
                return true;
            }
        } else if (n->liberties == 1) {
            return true;
        }
    }

    return false;
}

/*
RETURNS true if play is valid (ignoring ko rule)
*/
bool can_play_ignoring_ko(
    const cfg_board * cb,
    bool is_black,
    move m
) {
    assert(verify_cfg_board(cb));
    if (cb->p[m] != EMPTY) {
        return false;
    }

    if (cb->black_neighbors4[m] + cb->white_neighbors4[m] + out_neighbors4[m] < 4) {
        return true;
    }

    assert(cb->g[m] == NULL);
    group * n;

    if (!border_left[m]) {
        n = cb->g[m + LEFT];

        if (n == NULL) {
            return true;
        } else if (n->is_black == is_black) {
            if (n->liberties != 1) {
                return true;
            }
        } else {
            if (n->liberties == 1) {
                return true;
            }
        }
    }

    if (!border_right[m]) {
        n = cb->g[m + RIGHT];

        if (n == NULL) {
            return true;
        } else if (n->is_black == is_black) {
            if (n->liberties != 1) {
                return true;
            }
        } else {
            if (n->liberties == 1) {
                return true;
            }
        }
    }

    if (!border_top[m]) {
        n = cb->g[m + TOP];

        if (n == NULL) {
            return true;
        } else if (n->is_black == is_black) {
            if (n->liberties != 1) {
                return true;
            }
        } else {
            if (n->liberties == 1) {
                return true;
            }
        }
    }

    if (!border_bottom[m]) {
        n = cb->g[m + BOTTOM];

        if (n == NULL) {
            return true;
        } else if (n->is_black == is_black) {
            if (n->liberties != 1) {
                return true;
            }
        } else {
            if (n->liberties == 1) {
                return true;
            }
        }
    }

    return false;
}


/*
Frees the structure information dynamically allocated (not the actual cfg_board
structure).
*/
void cfg_board_free(
    cfg_board * cb
) {
    assert(verify_cfg_board(cb));

    for (u8 i = 0; i < cb->unique_groups_count; ++i) {
        just_delloc_group(cb->g[cb->unique_groups[i]]);
    }
}

/*
Print structure information for debugging.
*/
void fprint_cfg_board(
    FILE * fp,
    const cfg_board * cb
) {
    char * s = alloc();
    board_to_string(s, cb->p, cb->last_played, cb->last_eaten);
    fprintf(fp, "\nBOARD\n%s", s);
    release(s);

    fprintf(fp, "\nSTONES\n");
    for (move m = 0; m < TOTAL_BOARD_SIZ; ++m) {
        if (cb->g[m] == NULL) {
            fprintf(fp, "   %c", EMPTY_STONE_CHAR);
        } else {
            fprintf(fp, " %3u", cb->g[m]->stones.count);
        }

        if (((m + 1) % BOARD_SIZ) == 0) {
            fprintf(fp, "\n");
        }
    }

    fprintf(fp, "\nLIBERTIES\n");
    for (move m = 0; m < TOTAL_BOARD_SIZ; ++m) {
        if (cb->g[m] == NULL) {
            fprintf(fp, "   %c", EMPTY_STONE_CHAR);
        } else {
            fprintf(fp, " %3u", cb->g[m]->liberties);
        }

        if (((m + 1) % BOARD_SIZ) == 0) {
            fprintf(fp, "\n");
        }
    }

    fprintf(fp, "\nUNIQUES %u\n", cb->unique_groups_count);
    for (move m = 0; m < TOTAL_BOARD_SIZ; ++m) {
        if (cb->g[m] == NULL) {
            fprintf(fp, "   %c", EMPTY_STONE_CHAR);
        } else {
            fprintf(fp, " %3u", cb->g[m]->unique_groups_idx);
        }

        if (((m + 1) % BOARD_SIZ) == 0) {
            fprintf(fp, "\n");
        }
    }

    fprintf(fp, "\nHASHES %u\n", cb->unique_groups_count);
    for (move m = 0; m < TOTAL_BOARD_SIZ; ++m) {
        fprintf(fp, " %04x", cb->hash[m]);

        if (((m + 1) % BOARD_SIZ) == 0) {
            fprintf(fp, "\n");
        }
    }
}


/*
Verify the integrity of a CFG board structure.
*/
bool verify_cfg_board(
    const cfg_board * cb
) {
    if (cb == NULL) {
        fprintf(stderr, "error: verify_cfg_board: null reference\n");
        return false;
    }

    for (move m = 0; m < TOTAL_BOARD_SIZ; ++m) {
        if (cb->p[m] != EMPTY && cb->p[m] != BLACK_STONE && cb->p[m] != WHITE_STONE) {
            fprintf(stderr, "error: verify_cfg_board: illegal intersection color\n");
            return false;
        }

        if (cb->black_neighbors4[m] > 4) {
            fprintf(stderr, "error: verify_cfg_board: illegal neighbor count (1)\n");
            return false;
        }

        if (cb->white_neighbors4[m] > 4) {
            fprintf(stderr, "error: verify_cfg_board: illegal neighbor count (2)\n");
            return false;
        }

        if (cb->black_neighbors8[m] > 8) {
            fprintf(stderr, "error: verify_cfg_board: illegal neighbor count (3)\n");
            return false;
        }

        if (cb->white_neighbors8[m] > 8) {
            fprintf(stderr, "error: verify_cfg_board: illegal neighbor count (4)\n");
            return false;
        }

        if (cb->black_neighbors4[m] + cb->white_neighbors4[m] + out_neighbors4[m] > 4) {
            fprintf(stderr, "error: verify_cfg_board: illegal total neighbor count (1)\n");
            return false;
        }

        if (cb->black_neighbors8[m] + cb->white_neighbors8[m] + out_neighbors8[m] > 8) {
            fprintf(stderr, "error: verify_cfg_board: illegal total neighbor count (2)\n");
            return false;
        }

        if ((cb->p[m] == EMPTY) != (cb->g[m] == NULL)) {
            fprintf(stderr, "error: verify_cfg_board: mismatch between board and group\n");
            return false;
        }

        if (cb->g[m] != NULL) {
            group * g = cb->g[m];

            if (g->is_black != (cb->p[m] == BLACK_STONE)) {
                fprintf(stderr, "error: verify_cfg_board: group color mismatch\n");
                return false;
            }

            if (g->liberties == 0) {
                fprintf(stderr, "error: verify_cfg_board: zero number of liberties\n");
                return false;
            }

            if (cb->unique_groups[g->unique_groups_idx] != g->stones.coord[0]) {
                fprintf(stderr, "error: verify_cfg_board: unique groups linking error\n");
                return false;
            }

            if (g->liberties > 0 && g->liberties_min_coord >= BOARD_SIZ * BOARD_SIZ) {
                fprintf(stderr, "error: verify_cfg_board: illegal value of 1st liberty\n");
                return false;
            }

            if (g->stones.count == 0) {
                fprintf(stderr, "error: verify_cfg_board: illegal number of stones (0)\n");
                return false;
            }

            if (g->stones.count > TOTAL_BOARD_SIZ) {
                fprintf(stderr, "error: verify_cfg_board: illegal number of stones\n");
                return false;
            }

            for (move n = 0; n < g->stones.count; ++n) {
                move s = g->stones.coord[n];

                if (cb->p[s] == EMPTY) {
                    fprintf(stderr, "error: verify_cfg_board: group actually empty\n");
                    return false;
                }

                if (g->is_black != (cb->p[s] == BLACK_STONE)) {
                    fprintf(stderr, "error: verify_cfg_board: stone color mismatch\n");
                    return false;
                }

                if (g != cb->g[s]) {
                    fprintf(stderr, "error: verify_cfg_board: stone and links mismatch\n");
                    return false;
                }
            }

            if (g->neighbors_count > MAX_NEIGHBORS) {
                fprintf(stderr, "error: verify_cfg_board: illegal number of neighbors\n");
                return false;
            }

            for (u8 n = 0; n < g->neighbors_count; ++n) {
                for (u8 k = 0; k < n; ++k) {
                    if (g->neighbors[k] == g->neighbors[n]) {
                        fprintf(stderr, "error: verify_cfg_board: neighbor mismatch\n");
                        return false;
                    }
                }
            }
        }
    }

    if (!is_board_move(cb->last_eaten) && cb->last_eaten != NONE) {
        fprintf(stderr, "error: verify_cfg_board: illegal last eaten value\n");
        return false;
    }

    if (!is_board_move(cb->last_played) && cb->last_played != NONE && cb->last_played != PASS) {
        fprintf(stderr, "error: verify_cfg_board: illegal last played value\n");
        return false;
    }

    if (cb->empty.count > TOTAL_BOARD_SIZ) {
        fprintf(stderr, "error: verify_cfg_board: illegal number of empty points (%u)\n", cb->empty.count);
        return false;
    }

    for (move m = 0; m < cb->empty.count; ++m) {
        if (!is_board_move(m)) {
            fprintf(stderr, "error: verify_cfg_board: illegal empty intersection value\n");
            return false;
        }
    }

    return true;
}

