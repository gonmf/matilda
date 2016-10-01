/*
Miscellanea C string functions.
*/

#include "matilda.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "board.h"
#include "types.h"
#include "alloc.h"
#include "move.h"


/*
Validates if a filename does not contain illegal characters.
RETURNS true if filename is valid
*/
bool validate_filename(
    const char * filename
){
    if(filename == NULL || strlen(filename) == 0)
        return false;
    if(filename[0] == '/')
        return false;
    if(filename[0] == '-')
        return false;
    return (strstr(filename, "..") == NULL);
}

/*
RETURNS true if char is whitespace
*/
bool is_white_space(
    char c
){
    return c == 0x09 || c == 0x0a || c == 0x0b || c == 0x0c || c == 0x0d || c ==
        0x20;
}

/*
Searches for a character and cuts the string at that point if found.
*/
void line_cut_before(
    char * str,
    char c
){
    while(*str)
    {
        if(*str == c)
        {
            *str = 0;
            return;
        }
        ++str;
    }
}

/*
RETURNS pointer to started of trimmed string; or NULL
*/
char * trim(
    char * s
){
    while(1)
    {
        if(s[0] == 0)
            return NULL;
        if(is_white_space(s[0]))
            ++s;
        else
            break;
    }

    d32 i = strlen(s) - 1;
    while(i >= 0)
    {
        if(!is_white_space(s[i]))
        {
            s[i + 1] = 0;
            break;
        }
        --i;
    }
    return s;
}

/*
Converts an ASCII char to lower case.
*/
char low_char(
    char c
){
    if(c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c;
}

/*
Converts an ASCII C string to lower case.
*/
void lower_case(
    char * s
){
    u16 i = 0;
    while(s[i] != 0)
    {
        s[i] = low_char(s[i]);
        ++i;
    }
}

/*
Produces a copy of the string between the tokens; or empty
*/
void str_between(
    char * dst,
    const char * s,
    const char * start,
    const char * end
){
    char * t = strstr(s, start);
    if(t == NULL)
    {
        dst[0] = 0;
        return;
    }
    t += strlen(start);

    char * t2 = strstr(t, end);
    if(t2 == NULL)
    {
        dst[0] = 0;
        return;
    }

    d32 len = (d32)(t2 - t);
    memcpy(dst, t, len);
    dst[len] = 0;
}

/*
RETURNS true if s is equal or contains h
*/
bool starts_with(
    const char * s,
    const char * h
){
    u16 i = 0;
    while(h[i])
    {
        if(s[i] != h[i])
            return false;
        ++i;
    }
    return true;
}

/*
Parses a 32-bit signed integer.
RETURNS true if valid
*/
bool parse_int(
    d32 * i,
    const char * s
){
    if(s[0] != '-' && (s[0] < '0' || s[0] > '9'))
        return false;
    for(u32 j = 1; s[j]; ++j)
        if(s[j] < '0' || s[j] > '9')
            return false;
    *i = atoi(s);
    return true;
}

/*
Parses a floating point value.
RETURNS true if valid
*/
bool parse_float(
    double * d,
    const char * s
){
    bool dot_found = false;
    if(s[0] == '.')
        dot_found = true;
    else
        if(s[0] != '-' && (s[0] < '0' || s[0] > '9'))
            return false;

    for(u32 j = 1; s[j]; ++j)
    {
        if(s[j] == '.')
        {
            if(dot_found)
                return false;
            dot_found = true;
        }
        else
            if(s[j] < '0' || s[j] > '9')
                return false;
    }

    *d = atof(s);
    return true;
}


/*
Parses a GTP color token.
RETURNS true if valid
*/
bool parse_color(
    bool * is_black,
    const char * s
){
    char buf[8];
    strncpy(buf, s, 7);
    lower_case(buf);

    if(buf[0] == 'b' && (buf[1] == 0 || strcmp(buf, "black") == 0)){
        *is_black = true;
        return true;
    }
    if(buf[0] == 'w' && (buf[1] == 0 || strcmp(buf, "white") == 0)){
        *is_black = false;
        return true;
    }
    return false;
}

/*
Parses a GTP vertex (stone play or pass or resign).
Move m is codified as NONE if vertex is "resign".
RETURNS true if vertex is valid
*/
bool parse_gtp_vertex(
    const char * s,
    move * m
){
    char buf[8];
    strncpy(buf, s, 7);
    if(strlen(buf) < 2)
        return false;
    lower_case(buf);

    if(strcmp(buf, "pass") == 0)
    {
        *m = PASS;
        return true;
    }
    if(strcmp(buf, "resign") == 0)
    {
        *m = NONE;
        return true;
    }

    *m = coord_parse_alpha_num(buf);
    return (*m) != NONE;
}

/*
Converts a GTP move (play, pass or resign) to text.
*/
void coord_to_gtp_vertex(
    char * dst,
    move m
){
    if(m == PASS)
    {
        strncpy(dst, "pass", MAX_PAGE_SIZ);
        return;
    }

    if(m == NONE)
    {
        strncpy(dst, "null", MAX_PAGE_SIZ);
        return;
    }

    coord_to_alpha_num(dst, m);
}

/*
Damerau-levenshtein edit distance.
RETURNS the edit distance between two strings
*/
u8 levenshtein_dst(
    const char * s1,
    const char * s2
){
    u8 l1 = strlen(s1);
    u8 l2 = strlen(s2);

    if(l1 == 0)
        return l2;
    if(l2 == 0)
        return l1;

    u8 * v = alloc();
    for(u8 i = 0; i <= l2; ++i)
        v[i] = i;

    u8 ret;
    for(u8 i = 0; i < l1; ++i)
    {
        u8 min = i + 1;
        for(u8 j = 0; j < l2; ++j)
        {
            u8 cost = !((s1[i] == s2[j]) || (i && j && (s1[i - 1] == s2[j]) &&
                (s1[i] == s2[j - 1])));
            ret = MIN(MIN(v[j + 1] + 1, min + 1), v[j] + cost);
            v[j] = min;
            min = ret;
        }
        v[l2] = ret;
    }
    release(v);
    return ret;
}
