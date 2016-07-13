/*
Specification of helper functions for reading .pts files, which have rules in a
format similar to Fuego-style opening books; plus some useful functions for
reading handicap, hoshi and starting plays for MCTS.
*/

#ifndef MATILDA_PTS_FILE_H
#define MATILDA_PTS_FILE_H

#include "matilda.h"

#include "types.h"
#include "board.h"


/*
Open and prepare a file to be interpreted line by line.
*/
void open_rule_file(
    const char * filename
);

/*
Read the next rule line.
RETURNS rule line string
*/
char * read_next_rule();

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
Tests if a point is hoshi.
RETURNS true if hoshi
*/
bool is_hoshi_point(
    move m
);

/*
Retrieve a list of starting points for MCTS.
*/
void get_starting_points(
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
