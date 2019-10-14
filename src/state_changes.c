/*
For operations on a Go state like placing a stone, passing, etc.
Where performance is important prefer using cfg_board structure and related
functions (cfg_board.h and cfg_board.c).
*/

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "board.h"
#include "state_changes.h"
#include "zobrist.h"


static bool open_space_stone(
    const board * b,
    move m
) {
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);
    if (x > 0 && b->p[m + LEFT] == EMPTY)
        return true;
    if (x < BOARD_SIZ - 1 && b->p[m + RIGHT] == EMPTY)
        return true;
    if (y > 0 && b->p[m + TOP] == EMPTY)
        return true;
    if (y < BOARD_SIZ - 1 && b->p[m + BOTTOM] == EMPTY)
        return true;
    return false;
}

static bool surrounded_stone(
    const board * b,
    move m
) {
    u8 opt = (b->p[m] == BLACK_STONE) ? WHITE_STONE : BLACK_STONE;
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);

    if (x > 0 && b->p[m + LEFT] != opt)
        return false;
    if (x < BOARD_SIZ - 1 && b->p[m + RIGHT] != opt)
        return false;
    if (y > 0 && b->p[m + TOP] != opt)
        return false;
    if (y < BOARD_SIZ - 1 && b->p[m + BOTTOM] != opt)
        return false;
    return true;
}

static u8 _liberties(
    const board * b,
    move m,
    bool aux[static TOTAL_BOARD_SIZ],
    const u8 own_stone
) {
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);
    u8 ret = 0;
    if (x > 0 && aux[m + LEFT] == false) {
        aux[m + LEFT] = true;
        if (b->p[m + LEFT] == EMPTY)
            ret += 1;
        else
            if (b->p[m + LEFT] == own_stone)
                ret += _liberties(b, m + LEFT, aux, own_stone);
    }
    if (x < BOARD_SIZ - 1 && aux[m + RIGHT] == false) {
        aux[m + RIGHT] = true;
        if (b->p[m + RIGHT] == EMPTY)
            ret += 1;
        else
            if (b->p[m + RIGHT] == own_stone)
                ret += _liberties(b, m + RIGHT, aux, own_stone);
    }
    if (y > 0 && aux[m + TOP] == false) {
        aux[m + TOP] = true;
        if (b->p[m + TOP] == EMPTY)
            ret += 1;
        else
            if (b->p[m + TOP] == own_stone)
                ret += _liberties(b, m + TOP, aux, own_stone);
    }
    if (y < BOARD_SIZ - 1 && aux[m + BOTTOM] == false) {
        aux[m + BOTTOM] = true;
        if (b->p[m + BOTTOM] == EMPTY)
            ret += 1;
        else
            if (b->p[m + BOTTOM] == own_stone)
                ret += _liberties(b, m + BOTTOM, aux, own_stone);
    }
    return ret;
}

/*
Recursively counts the liberties after playing.
RETURNS liberties after playing regardless if play is legal
*/
u8 libs_after_play_slow(
    const board * b,
    bool is_black,
    move m,
    u16 * caps
) {
    assert(b->p[m] == EMPTY);
    /* First play and capture whats needs capturing */
    board tmp;
    memcpy(&tmp, b, sizeof(board));
    bool cp = attempt_play_slow(&tmp, is_black, m);
    if (!cp) {
        *caps = 0;
        return 0;
    }
    *caps = abs(stone_diff(b->p) - stone_diff(tmp.p)) - 1;
    /* Then count liberties */
    bool aux[TOTAL_BOARD_SIZ];
    memset(aux, false, TOTAL_BOARD_SIZ * sizeof(bool));
    aux[m] = true;
    return _liberties(&tmp, m, aux, is_black ? BLACK_STONE : WHITE_STONE);
}

/*
Recursively counts the liberties of a group.
RETURNS liberties of the group
*/
u8 slow_liberty_count(
    const board * b,
    move m
) {
    assert(b->p[m] != EMPTY);
    bool aux[TOTAL_BOARD_SIZ];
    memset(aux, false, TOTAL_BOARD_SIZ * sizeof(bool));
    aux[m] = true;
    return _liberties(b, m, aux, b->p[m]);
}

/* returns TRUE if at least one liberty is found */
static bool _is_alive(
    const board * b,
    move m,
    u8 value,
    bool aux[static TOTAL_BOARD_SIZ]
) {
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);

    if (x > 0) {
        if (b->p[m + LEFT] == EMPTY)
            return true;
        if (aux[m + LEFT] == false && b->p[m + LEFT] == value) {
            aux[m + LEFT] = true;
            if (_is_alive(b, m + LEFT, value, aux))
                return true;
        }
    }
    if (x < BOARD_SIZ - 1) {
        if (b->p[m + RIGHT] == EMPTY)
            return true;
        if (aux[m + RIGHT] == false && b->p[m + RIGHT] == value) {
            aux[m + RIGHT] = true;
            if (_is_alive(b, m + RIGHT, value, aux))
                return true;
        }
    }
    if (y > 0) {
        if (b->p[m + TOP] == EMPTY)
            return true;
        if (aux[m + TOP] == false && b->p[m + TOP] == value) {
            aux[m + TOP] = true;
            if (_is_alive(b, m + TOP, value, aux))
                return true;
        }
    }
    if (y < BOARD_SIZ - 1) {
        if (b->p[m + BOTTOM] == EMPTY)
            return true;
        if (aux[m + BOTTOM] == false && b->p[m + BOTTOM] == value) {
            aux[m + BOTTOM] = true;
            if (_is_alive(b, m + BOTTOM, value, aux))
                return true;
        }
    }
    return false;
}

static bool is_alive(
    const board * b,
    move m
) {
    if (open_space_stone(b, m))
        return true;
    if (surrounded_stone(b, m))
        return false;

    bool aux[TOTAL_BOARD_SIZ];
    memset(aux, false, TOTAL_BOARD_SIZ * sizeof(bool));
    aux[m] = true;
    return _is_alive(b, m, b->p[m], aux);
}

static u16 _capture(
    board * b,
    move m,
    u8 value
) {
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);
    u16 ret = 1;
    b->p[m] = EMPTY;
    if (x > 0 && b->p[m + LEFT] == value)
        ret += _capture(b, m + LEFT, value);
    if (x < BOARD_SIZ - 1 && b->p[m + RIGHT] == value)
        ret += _capture(b, m + RIGHT, value);
    if (y > 0 && b->p[m + TOP] == value)
        ret += _capture(b, m + TOP, value);
    if (y < BOARD_SIZ - 1 && b->p[m + BOTTOM] == value)
        ret += _capture(b, m + BOTTOM, value);
    return ret;
}

static u16 capture(
    board * b,
    move m
) {
    return _capture(b, m, b->p[m]);
}

/*
Note: testing the last eaten position is not enough because current play might
be a multiple stone capture, thus not subject to ko rule.
RETURNS true if ko detected and play is invalid
*/
bool test_ko(
    board * b,
    move m,
    u8 own_stone /* attention */
) {
    b->p[m] = own_stone;
    bool ko_detected = (m == b->last_eaten && surrounded_stone(b,
        b->last_played));
    b->p[m] = EMPTY;
    return ko_detected;
}

/*
Performs a pass, updating the necessary information.
*/
void pass(
    board * b
) {
    b->last_played = PASS;
    b->last_eaten = NONE;
}

static u16 _capture_and_update_hash(
    board * b,
    move m,
    u8 value,
    u64 * zobrist_hash
) {
    assert(is_board_move(m));
    u16 ret = 1;
    zobrist_update_hash(zobrist_hash, m, b->p[m]);
    b->p[m] = EMPTY;
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);
    if (x > 0 && b->p[m + LEFT] == value)
        ret += _capture_and_update_hash(b, m + LEFT, value, zobrist_hash);
    if (x < BOARD_SIZ - 1 && b->p[m + RIGHT] == value)
        ret += _capture_and_update_hash(b, m + RIGHT, value, zobrist_hash);
    if (y > 0 && b->p[m + TOP] == value)
        ret += _capture_and_update_hash(b, m + TOP, value, zobrist_hash);
    if (y < BOARD_SIZ - 1 && b->p[m + BOTTOM] == value)
        ret += _capture_and_update_hash(b, m + BOTTOM, value, zobrist_hash);
    return ret;
}

static u16 capture_and_update_hash(
    board * b,
    move m,
    u64 * zobrist_hash
) {
    return _capture_and_update_hash(b, m, b->p[m], zobrist_hash);
}

/*
Plays ignoring if it is legal.
*/
void just_play_slow2(
    board * b,
    bool is_black,
    move m,
    u16 * captured
) {
    assert(is_board_move(m));
    assert(b->p[m] == EMPTY);
    u8 own;
    u8 opt;
    if (is_black) {
        own = BLACK_STONE;
        opt = WHITE_STONE;
    } else {
        own = WHITE_STONE;
        opt = BLACK_STONE;
    }
    b->p[m] = own;

    move one_stone_captured = NONE;
    u16 caps = 0;
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);

    /* captured dead opponent groups */
    if (x > 0 && b->p[m + LEFT] == opt && !is_alive(b, m + LEFT)) {
        caps += capture(b, m + LEFT);
        one_stone_captured = m + LEFT;
    }
    if (x < BOARD_SIZ - 1 && b->p[m + RIGHT] == opt && !is_alive(b, m + RIGHT)) {
        caps += capture(b, m + RIGHT);
        one_stone_captured = m + RIGHT;
    }
    if (y > 0 && b->p[m + TOP] == opt && !is_alive(b, m + TOP)) {
        caps += capture(b, m + TOP);
        one_stone_captured = m + TOP;
    }
    if (y < BOARD_SIZ - 1 && b->p[m + BOTTOM] == opt && !is_alive(b, m + BOTTOM)) {
        caps += capture(b, m + BOTTOM);
        one_stone_captured = m + BOTTOM;
    }

    if (caps == 1)
        b->last_eaten = one_stone_captured;
    else
        b->last_eaten = NONE;

    b->last_played = m;

    *captured = caps;
}

/*
Plays ignoring if it is legal.
*/
void just_play_slow(
    board * b,
    bool is_black,
    move m
) {
    u16 _ignored;
    just_play_slow2(b, is_black, m, &_ignored);
}

/*
Plays ignoring if it is legal.
Also updates an associated Zobrist hash of the previous state.
RETURNS updated Zobrist hash
*/
u64 just_play_slow_and_get_hash(
    board * b,
    bool is_black,
    move m,
    u64 zobrist_hash
) {
    assert(is_board_move(m));
    assert(b->p[m] == EMPTY);
    u64 ret = zobrist_hash;
    u8 own;
    u8 opt;
    if (is_black) {
        own = BLACK_STONE;
        opt = WHITE_STONE;
    } else {
        own = WHITE_STONE;
        opt = BLACK_STONE;
    }
    zobrist_update_hash(&ret, m, own);
    b->p[m] = own;

    move one_stone_captured = NONE;
    u16 captured = 0;
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);

    /* captured dead opponent groups */
    if (x > 0 && b->p[m + LEFT] == opt && !is_alive(b, m + LEFT)) {
        captured += capture_and_update_hash(b, m + LEFT, &ret);
        one_stone_captured = m + LEFT;
    }
    if (x < BOARD_SIZ - 1 && b->p[m + RIGHT] == opt && !is_alive(b, m + RIGHT)) {
        captured += capture_and_update_hash(b, m + RIGHT, &ret);
        one_stone_captured = m + RIGHT;
    }
    if (y > 0 && b->p[m + TOP] == opt && !is_alive(b, m + TOP)) {
        captured += capture_and_update_hash(b, m + TOP, &ret);
        one_stone_captured = m + TOP;
    }
    if (y < BOARD_SIZ - 1 && b->p[m + BOTTOM] == opt && !is_alive(b, m + BOTTOM)) {
        captured += capture_and_update_hash(b, m + BOTTOM, &ret);
        one_stone_captured = m + BOTTOM;
    }

    if (captured == 1)
        b->last_eaten = one_stone_captured;
    else
        b->last_eaten = NONE;

    b->last_played = m;
    return ret;
}

/*
Attempts to play, testing if it is legal.
If play is illegal original board is not changed.
RETURNS true if play was successful
*/
bool attempt_play_slow(
    board * b,
    bool is_black,
    move m
) {
    assert(is_board_move(m));
    if (b->p[m] != EMPTY)
        return false;
    u8 own;
    u8 opt;
    if (is_black) {
        own = BLACK_STONE;
        opt = WHITE_STONE;
    } else {
        own = WHITE_STONE;
        opt = BLACK_STONE;
    }

    if (test_ko(b, m, own)) /* ko detected */
        return false;

    move one_stone_captured = NONE;
    b->p[m] = own;
    u16 captured = 0;
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);

    /* captured dead opponent groups */
    if (x > 0 && b->p[m + LEFT] == opt && !is_alive(b, m + LEFT)) {
        captured += capture(b, m + LEFT);
        one_stone_captured = m + LEFT;
    }
    if (x < BOARD_SIZ - 1 && b->p[m + RIGHT] == opt && !is_alive(b, m + RIGHT)) {
        captured += capture(b, m + RIGHT);
        one_stone_captured = m + RIGHT;
    }
    if (y > 0 && b->p[m + TOP] == opt && !is_alive(b, m + TOP)) {
        captured += capture(b, m + TOP);
        one_stone_captured = m + TOP;
    }
    if (y < BOARD_SIZ - 1 && b->p[m + BOTTOM] == opt && !is_alive(b, m + BOTTOM)) {
        captured += capture(b, m + BOTTOM);
        one_stone_captured = m + BOTTOM;
    }

    if (captured == 0 && !is_alive(b, m)) {
        b->p[m] = EMPTY;
        return false;
    }

    if (captured == 1)
        b->last_eaten = one_stone_captured;
    else
        b->last_eaten = NONE;

    b->last_played = m;
    return true;
}

/*
Tests if play is valid disregarding superko rule.
Does not change original board.
RETURNS true if play is apparently legal
*/
bool can_play_slow(
    board * b,
    bool is_black,
    move m
) {
    assert(is_board_move(m));
    if (b->p[m] != EMPTY)
        return false;
    u8 own;
    u8 opt;
    if (is_black) {
        own = BLACK_STONE;
        opt = WHITE_STONE;
    } else {
        own = WHITE_STONE;
        opt = BLACK_STONE;
    }

    if (test_ko(b, m, own)) /* ko detected */
        return false;

    b->p[m] = own;
    u8 x;
    u8 y;
    move_to_coord(m, &x, &y);

    /* captured dead opponent groups */
    if (x > 0 && b->p[m + LEFT] == opt && !is_alive(b, m + LEFT)) {
        b->p[m] = EMPTY;
        return true;
    }
    if (x < BOARD_SIZ - 1 && b->p[m + RIGHT] == opt && !is_alive(b, m + RIGHT)) {
        b->p[m] = EMPTY;
        return true;
    }
    if (y > 0 && b->p[m + TOP] == opt && !is_alive(b, m + TOP)) {
        b->p[m] = EMPTY;
        return true;
    }
    if (y < BOARD_SIZ - 1 && b->p[m + BOTTOM] == opt && !is_alive(b, m + BOTTOM)) {
        b->p[m] = EMPTY;
        return true;
    }

    bool ret = is_alive(b, m);
    b->p[m] = EMPTY;
    return ret;
}


