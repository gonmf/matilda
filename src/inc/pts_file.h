/*
Specification of helper functions for reading .pts files, which have rules in a
format similar to Fuego-style opening books; plus some useful functions for
reading handicap, hoshi and starting plays for MCTS.
*/

#ifndef MATILDA_PTS_FILE_H
#define MATILDA_PTS_FILE_H

#include "matilda.h"

#include "types.h"

/*
Include as external to access
bool is_handicap[TOTAL_BOARD_SIZ];
bool is_hoshi[TOTAL_BOARD_SIZ];
bool is_starting[TOTAL_BOARD_SIZ];
*/

/*
Open and prepare a file to be interpreted line by line.
*/
void open_rule_file(
    const char * filename
);

/*
Read the next rule line.
*/
void read_next_rule(
    char * dst
);

/*
Interpret a string as a rule line, filling a move_seq structure with the points.
*/
void interpret_rule_as_pts_list(
    move_seq * dst,
    const char * src
);

/*
Close the rule file previously opened.
*/
void close_rule_file();


/*
Retrieve an ordered list of suggested handicap points.
*/
void get_ordered_handicap(
    move_seq * dst
);


/*
Load handicap points.
*/
void load_handicap_points();

/*
Load hoshi points.
*/
void load_hoshi_points();

/*
Load starting MCTS points.
*/
void load_starting_points();


#endif
