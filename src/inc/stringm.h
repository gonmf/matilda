/*
Miscellanea C string functions.
*/

#ifndef MATILDA_STRINGM_H
#define MATILDA_STRINGM_H

#include "config.h"

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
Produces a copy of the string between the tokens; or empty
*/
void str_between(
    char * dst,
    const char * restrict s,
    const char * restrict start,
    const char * restrict end
);

/*
RETURNS true if s is equal or contains h
*/
bool starts_with(
    const char * restrict s,
    const char * restrict h
);

/*
Parses a 32-bit signed integer.
RETURNS true if valid
*/
bool parse_int(
    d32 * i,
    const char * s
);

/*
Parses a 32-bit unsigned integer.
RETURNS true if valid
*/
bool parse_uint(
    u32 * i,
    const char * s
);

/*
Parses a floating point value.
RETURNS true if valid
*/
bool parse_float(
    double * d,
    const char * s
);

/*
Parses a GTP color token.
RETURNS true if valid
*/
bool parse_color(
    bool * is_black,
    const char * s
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
*/
void coord_to_gtp_vertex(
    char * dst,
    move m
);

/*
Format a quantity of bytes as string with SI units.
*/
void format_mem_size(
    char * dst,
    u64 bytes
);

/*
Format a quantity of milliseconds as a string with SI units.
*/
void format_nr_millis(
    char * dst,
    u64 millis
);

/*
Damerau-levenshtein edit distance.
RETURNS the edit distance between two strings
*/
u8 levenshtein_dst(
    const char * restrict s1,
    const char * restrict s2
);


#endif
