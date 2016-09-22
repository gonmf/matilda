/*
Functions that control the flow of information of Matilda as a complete Go
playing program. Allows executing strategies with some abstraction, performing
maintenance if needed.
*/

#ifndef MATILDA_ENGINE_H
#define MATILDA_ENGINE_H

#include "matilda.h"

#include "types.h"
#include "board.h"


#define BENCHMARK_TIME 60 /* seconds */


/*
Produce a short version string. Does not include program name.
*/
void version_string(
    char * dst
);

/*
Obtains the current data folder path. It may be absolute or relative and ends
with a path separator.
RETURNS folder path
*/
const char * get_data_folder();

/*
Sets the new data folder path. If the path is too long, short, or otherwise
invalid, nothing is changed and false is returned.
RETURNS true on success
*/
bool set_data_folder(
    const char * s
);

/*
Set whether to attempt to use, or not, opening books prior to MCTS.
*/
void set_use_of_opening_book(
    bool use_ob
);

/*
Evaluates the position given the time available to think, by using a number of
strategies in succession.
RETURNS true if a play or pass is suggested instead of resigning
*/
bool evaluate_position_timed(
    const board * b,
    bool is_black,
    out_board * out_b,
    u64 stop_time,
    u64 early_stop_time
);

/*
Evaluates the position with the number of simulations available.
RETURNS true if a play or pass is suggested instead of resigning
*/
bool evaluate_position_sims(
    const board * b,
    bool is_black,
    out_board * out_b,
    u32 simulations
);

/*
Evaluate the position for a short amount of time, ignoring the quality matrix
produced.
*/
void evaluate_in_background(
    const board * b,
    bool is_black
);

/*
Inform that we are currently between matches and proceed with the maintenance
that is suitable at the moment.
*/
void new_match_maintenance();

/*
Perform between-turn maintenance. If there is any information from MCTS-UCT that
can be freed, it will be done to the states not reachable from state b played by
is_black.
*/
void opt_turn_maintenance(
    const board * b,
    bool is_black
);

/*
Asserts the ./data folder exists, closing the program and warning the user if it
doesn't.
*/
void assert_data_folder_exists();

#endif
