/*
Miscellanea C string functions.
*/

#ifndef MATILDA_STRINGM_H
#define MATILDA_STRINGM_H

#include "matilda.h"

#include "types.h"
#include "move.h"


/*
Validates if a filename does not contain illegal characters.
RETURNS true if filename is valid
*/
bool validate_filename(
    const char * filename
);

/*
RETURNS true if char is whitespace
*/
bool is_white_space(
    char c
);

/*
Searches for a character and cuts the string at that point if found.
*/
void line_cut_before(
    char * str,
    char c
);

/*
RETURNS pointer to started of trimmed string; or NULL
*/
char * trim(
    char * s
);

/*
Converts an ASCII char to lower case.
*/
char low_char(
    char c
);

/*
Converts an ASCII C string to lower case.
*/
void lower_case(
    char * s
);

/*
Not thread-safe.
RETURNS a copy of the string between the tokens; or NULL
*/
char * str_between(
    const char * s,
    const char * start,
    const char * end
);

/*
RETURNS true if s is equal or contains h
*/
bool starts_with(
    const char * s,
    const char * h
);

/*
Parses a 32-bit signed integer.
RETURNS true if valid
*/
bool parse_int(
    const char * s,
    d32 * i
);

/*
Parses a floating point value.
RETURNS true if valid
*/
bool parse_float(
    const char * s,
    double * d
);

/*
Parses a GTP color token.
RETURNS true if valid
*/
bool parse_color(
    const char * s,
    bool * is_black
);

/*
Parses a GTP vertex (stone play or pass or resign).
Move m is codified as NONE if vertex is "resign".
RETURNS true if vertex is valid
*/
bool parse_gtp_vertex(
    const char * s,
    move * m
);

/*
Converts a GTP move (play, pass or resign) to text.
Not thread-safe.
RETURNS move representation
*/
const char * coord_to_gtp_vertex(
    move m
);

/*
Damerau-levenshtein edit distance.
RETURNS the edit distance between two strings
*/
u8 levenshtein_dst(
    const char * s1,
    const char * s2
);


#endif

