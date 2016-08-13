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


#ifndef MATILDA_GAME_RECORD_H
#define MATILDA_GAME_RECORD_H

#include "matilda.h"

#include "types.h"
#include "move.h"
#include "board.h"

#define MAX_GAME_LENGTH (TOTAL_BOARD_SIZ * 8)

#define MAX_PLAYER_NAME_SIZ 32

typedef struct __game_record_ {
	char black_name[MAX_PLAYER_NAME_SIZ];
	char white_name[MAX_PLAYER_NAME_SIZ];
	move_seq handicap_stones;
	move moves[MAX_GAME_LENGTH];
	u16 turns;
	bool game_finished;
	bool resignation;
	d16 final_score; /* 0 if finished by resignation/time/forfeit */
} game_record;



/*
Clear the entire game record including handicap stones.
*/
void clear_game_record(
    game_record * gr
);

/*
Adds a play to the game record and advances its state. Play legality is not
verified. If the player is not the expected player to play (out of order
anomaly) the error is logged and the program exits.
*/
void add_play(
    game_record * gr,
    move m
);

/*
Adds a play to the game record and advances its state. Play legality is not
verified. If the player is not the expected player to play (out of order
anomaly) the opponents turn is skipped.
*/
void add_play_out_of_order(
    game_record * gr,
    bool is_black,
    move m
);

/*
Print a text representation of the game record to the fp file stream.
*/
void fprint_game_record(
    FILE * fp,
    const game_record * gr
);

/*
Returns whether a play is a superko violation. Does not test other legality
restrictions.
RETURNS true if illegal by positional superko.
*/
bool superko_violation(
    const game_record * gr,
    bool is_black,
    move m
);

/*
Tests if a play is legal including ko and superko.
Group suicides are prohibited.
RETURNS true if play is legal
*/
bool play_is_legal(
    const game_record * gr,
    move m,
    bool is_black
);

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
);

/*
Given the current game context select the best play as evaluated.
If several plays have the same quality one of them is selected randomly.
RETURNS the move selected, or a pass
*/
move select_play_fast(
    const out_board * evaluation
);

/*
Attempts to undo the last play.
RETURNS true if a play was undone
*/
bool undo_last_play(
    game_record * gr
);

/*
Adds a handicap stone to a yet-to-start game.
RETURNS true if stone was added
*/
bool add_handicap_stone(
    game_record * gr,
    move m
);


/*
Produce the current game state to board form.
*/
void current_game_state(
    board * dst,
    const game_record * src
);

/*
Produces the first game state, with handicap stones placed.
*/
void first_game_state(
    board * dst,
    const game_record * src
);

/*
Retrieves the first player color, taking handicap stones to consideration.
RETURNS the first player color
*/
bool first_player_color(
    const game_record * gr
);

/*
Retrieves the current player color, taking handicap stones to consideration.
RETURNS the current player color
*/
bool current_player_color(
    const game_record * gr
);

#endif

