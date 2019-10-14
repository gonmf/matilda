/*
Game record of an entire match.

For manipulating game records, including selecting a play that does not violate
positional superko, plus testing if playing in order and dealing with undos and
handicap stones.

When manipulating a game record only use the functions bellow; do not modify the
internal information of the game_record struct.

On positional superkos:
http://www.weddslist.com/kgs/past/superko.html
*/

#include "config.h"

#include <stdlib.h>
#include <math.h> /* fabs */
#include <string.h>

#include "alloc.h"
#include "board.h"
#include "flog.h"
#include "game_record.h"
#include "move.h"
#include "randg.h"
#include "state_changes.h"
#include "stringm.h"
#include "types.h"

static board current_game_bak;
static bool current_game_bak_set = false;

static void apply_handicap_stones(
    board * b,
    const game_record * gr
) {
    for (u16 i = 0; i < gr->handicap_stones.count; ++i)
        just_play_slow(b, true, gr->handicap_stones.coord[i]);
}

/*
Clear the entire game record including handicap stones.
*/
void clear_game_record(
    game_record * gr
) {
    snprintf(gr->black_name, MAX_PLAYER_NAME_SIZ, "black");
    snprintf(gr->white_name, MAX_PLAYER_NAME_SIZ, "white");
    gr->handicap_stones.count = 0;
    gr->turns = 0;
    gr->finished = gr->resignation = gr->timeout = gr->player_names_set = false;
    current_game_bak_set = false;
}

/*
Adds a play to the game record and advances its state. Play legality is not
verified. If the player is not the expected player to play (out of order
anomaly) the error is logged and the program exits.
*/
void add_play(
    game_record * gr,
    move m
) {
    gr->moves[gr->turns] = m;
    gr->turns++;
    if (gr->turns == MAX_GAME_LENGTH)
        flog_crit("gr", "the maximum number of plays has been reached");
    gr->finished = false;
    current_game_bak_set = false;
}

/*
Adds a play to the game record and advances its state. Play legality is not
verified. If the player is not the expected player to play (out of order
anomaly) the opponents turn is skipped.
*/
void add_play_out_of_order(
    game_record * gr,
    bool is_black,
    move m
) {
    bool is_black_from_turns = current_player_color(gr);
    if (is_black_from_turns != is_black)
        add_play(gr, NONE);
    add_play(gr, m);
}

/*
Write a text representation of the game record in the string buffer specified.
*/
void game_record_to_string(
    char * buf,
    u32 buf_siz,
    const game_record * gr
) {
    u32 idx = snprintf(buf, buf_siz, "White (%c): %s\nBlack (%c): %s\n",
        WHITE_STONE_CHAR, gr->white_name, BLACK_STONE_CHAR, gr->black_name);
    if (gr->finished) {
        if (gr->resignation)
            idx += snprintf(buf + idx, buf_siz - idx,
                "Winner: %s by resignation\n", gr->final_score > 0 ?
                gr->black_name : gr->white_name);
        else
            if (gr->timeout)
                idx += snprintf(buf + idx, buf_siz - idx,
                    "Winner: %s by timeout\n", gr->final_score > 0 ?
                    gr->black_name : gr->white_name);
            else
                idx += snprintf(buf + idx, buf_siz - idx,
                    "Winner: %s by %.1f points\n", gr->final_score > 0 ?
                    gr->black_name : gr->white_name, fabs(gr->final_score) / 2.0);
    }

    char * v = alloc();

    if (gr->handicap_stones.count > 0) {
        idx += snprintf(buf + idx, buf_siz - idx, "Handicap stones (%u):",
            gr->handicap_stones.count);
        for (u16 i = 0; i < gr->handicap_stones.count; ++i) {
#if EUROPEAN_NOTATION
            coord_to_alpha_num(v, gr->handicap_stones.coord[i]);
#else
            coord_to_num_num(v, gr->handicap_stones.coord[i]);
#endif
            idx += snprintf(buf + idx, buf_siz - idx, " %s", v);
        }
        idx += snprintf(buf + idx, buf_siz - idx, "\n");
    }

    if (gr->turns > 0) {
        idx += snprintf(buf + idx, buf_siz - idx, "Plays (%u):", gr->turns);
        for (u16 i = 0; i < gr->turns; ++i)
            if (is_board_move(gr->moves[i]))
            {
#if EUROPEAN_NOTATION
                coord_to_alpha_num(v, gr->moves[i]);
                lower_case(v);
#else
                coord_to_num_num(v, gr->moves[i]);
#endif
                if (gr->handicap_stones.count == 0)
                    idx += snprintf(buf + idx, buf_siz - idx, " %c%s",
                        (i & 1) == 0 ? 'B' : 'W', v);
                else
                    idx += snprintf(buf + idx, buf_siz - idx, " %c%s",
                        (i & 1) == 1 ? 'B' : 'W', v);
            }
            else
            {
                if (gr->handicap_stones.count == 0)
                    idx += snprintf(buf + idx, buf_siz - idx, " %c--",
                        (i & 1) == 0 ? 'B' : 'W');
                else
                    idx += snprintf(buf + idx, buf_siz - idx, " %c--",
                        (i & 1) == 1 ? 'B' : 'W');
            }

        idx += snprintf(buf + idx, buf_siz - idx, "\n");
    }

    release(v);
}

/*
Print a text representation of the game record to the fp file stream.
*/
void fprint_game_record(
    FILE * fp,
    const game_record * gr
) {
    char * s = malloc(MAX_FILE_SIZ);
    if (s == NULL)
        flog_crit("gr", "system out of memory");
    game_record_to_string(s, MAX_FILE_SIZ, gr);
    fprintf(fp, "%s", s);
    free(s);
}

/*
Returns whether a play is a superko violation. Does not test other legality
restrictions.
RETURNS true if illegal by positional superko.
*/
bool test_superko(
    const game_record * gr,
    bool is_black,
    move m
) {
    board tmp;
    first_game_state(&tmp, gr);

    board current_state;
    current_game_state(&current_state, gr);

    /*
    State after playing
    */
    just_play_slow(&current_state, is_black, m);

    bool captured = false;

    bool is_b = first_player_color(gr);
    for (u16 i = 0; i < gr->turns; ++i) {
        if (is_board_move(gr->moves[i])) {
            u16 caps;
            just_play_slow2(&tmp, is_b, gr->moves[i], &caps);
            if (caps > 0)
                captured = true;

            if (captured && memcmp(tmp.p, current_state.p, TOTAL_BOARD_SIZ) == 0)
                return true;
        }
        else
            pass(&tmp);
        is_b = !is_b;
    }

    return false;
}

/*
Tests if a play is legal including ko and superko.
Group suicides are prohibited.
RETURNS true if play is legal
*/
bool play_is_legal(
    const game_record * gr,
    bool is_black,
    move m
) {
    if (m == PASS)
        return true;
    if (!is_board_move(m))
        return false;

    board tmp;
    current_game_state(&tmp, gr);
    if (!can_play_slow(&tmp, is_black, m))
        return false;

    if (gr->turns > 0 && test_superko(gr, is_black, m))
        return false;

    return true;
}

/*
Given the current game context select the best play as evaluated without
violating the superko rule. If several plays have the same quality one of them
is selected randomly. If passing is of the same quality as the best plays, one
of the best plays is selected instead of passing.
RETURNS the move selected, or a pass
*/
move select_play(
    const out_board * evaluation,
    bool is_black,
    const game_record * gr
) {
    double best_value = evaluation->pass;
    u32 best_play = PASS;
    /*
    Answer immediately if possible.
    */
    for (move m = 0; m < TOTAL_BOARD_SIZ; ++m)
        if (evaluation->tested[m] && evaluation->value[m] >= best_value) {
            best_value = evaluation->value[m];
            best_play = m;
        }
    if (best_play == PASS || gr->turns == 0 ||
        !test_superko(gr, is_black, best_play))
        return best_play;


    /*
    Slow answer
    */
    double qualities[TOTAL_BOARD_SIZ];
    move playable[TOTAL_BOARD_SIZ];
    u16 playable_count = 0;

    /*
    Enumerate legal plays
    */
    for (move m = 0; m < TOTAL_BOARD_SIZ; ++m)
        if (evaluation->tested[m] && evaluation->value[m] >= evaluation->pass) {
            playable[playable_count] = m;
            qualities[playable_count] = evaluation->value[m];
            playable_count++;
        }

    /*
    Sort legal plays
    */
    bool rep;
    do
    {
        rep = false;
        for (u16 i = 1; i < playable_count; ++i)
            if (qualities[i - 1] < qualities[i])
            {
                double tmp1 = qualities[i - 1];
                qualities[i - 1] = qualities[i];
                qualities[i] = tmp1;
                move tmp2 = playable[i - 1];
                playable[i - 1] = playable[i];
                playable[i] = tmp2;
                rep = true;
            }
    }
    while (rep);

    /*
    Test superko in order of quality
    */
    for (u16 i = 0; i < playable_count; ++i) {
        move m = playable[i];
        if (gr->turns == 0 || !test_superko(gr, is_black, m))
            return m;
    }

    return PASS;
}

/*
Given the current game context select the best play as evaluated.
If several plays have the same quality one of them is selected randomly.
RETURNS the move selected, or a pass
*/
move select_play_fast(
    const out_board * evaluation
) {
    move best_play = PASS;
    double best_value = evaluation->pass;

    for (move m = 0; m < TOTAL_BOARD_SIZ; ++m)
        if (evaluation->tested[m] && evaluation->value[m] >= best_value) {
            best_value = evaluation->value[m];
            best_play = m;
        }

    return best_play;
}

/*
Attempts to undo the last play.
RETURNS true if a play was undone
*/
bool undo_last_play(
    game_record * gr
) {
    if (gr->turns == 0)
        return false;

    board tmp;
    first_game_state(&tmp, gr);

    gr->turns--;

    bool is_black = first_player_color(gr);
    for (u16 i = 0; i < gr->turns; ++i) {
        if (is_board_move(gr->moves[i]))
            just_play_slow(&tmp, is_black, gr->moves[i]);
        else
            pass(&tmp);
        is_black = !is_black;
    }

    gr->finished = false;

    current_game_bak_set = false;
    return true;
}

/*
Adds a handicap stone to a yet-to-start game.
RETURNS true if stone was added
*/
bool add_handicap_stone(
    game_record * gr,
    move m
) {
    if (gr->turns != 0 || !is_board_move(m))
        return false;

    if (gr->handicap_stones.count >= TOTAL_BOARD_SIZ - 1)
        return false;

    for (u16 i = 0; i < gr->handicap_stones.count; ++i)
        if (gr->handicap_stones.coord[i] == m)
            return false;

    gr->handicap_stones.coord[gr->handicap_stones.count] = m;
    gr->handicap_stones.count++;
    current_game_bak_set = false;
    return true;
}

/*
Produce the current game state to board form.
*/
void current_game_state(
    board * dst,
    const game_record * src
) {
    if (!current_game_bak_set) {
        clear_board(&current_game_bak);
        apply_handicap_stones(&current_game_bak, src);

        bool is_black = first_player_color(src);
        for (u16 i = 0; i < src->turns; ++i) {
            if (is_board_move(src->moves[i]))
                just_play_slow(&current_game_bak, is_black, src->moves[i]);
            else
                pass(&current_game_bak);
            is_black = !is_black;
        }
        current_game_bak_set = true;
    }

    memcpy(dst, &current_game_bak, sizeof(board));
}

/*
Produces the first game state, with handicap stones placed.
*/
void first_game_state(
    board * dst,
    const game_record * src
) {
    clear_board(dst);
    apply_handicap_stones(dst, src);
}

/*
Retrieves the first player color, taking handicap stones to consideration.
RETURNS the first player color
*/
bool first_player_color(
    const game_record * gr
) {
    return (gr->handicap_stones.count == 0);
}

/*
Retrieves the current player color, taking handicap stones to consideration.
RETURNS the current player color
*/
bool current_player_color(
    const game_record * gr
) {
    if (gr->handicap_stones.count == 0)
        return ((gr->turns & 1) == 0);
    return ((gr->turns & 1) == 1);
}

