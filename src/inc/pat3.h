/*
Functions that support the use of small 3x3 patterns hand crafted by the authors
of GNU Go, MoGo and others over the years.

The life of these patterns is as follow:
 * On startup a pat3 file is loaded with a number of 3x3 patterns suggesting
 play at the center intersection. The pattern is flipped and rotated and stored
 in a hash table for both players (with the color inverted for white). They are
 stored in their 16-bit value form.

 * In MCTS each candidate position can be transposed to a 3x3 array, which fixed
 out of bounds codification, fliped and rotated (but the color remains the same)
 and searched for in the appropriate hash table.
*/

#ifndef MATILDA_PAT3_H
#define MATILDA_PAT3_H

#include "matilda.h"

#include "types.h"
#include "board.h"

#define USE_PATTERN_WEIGHTS true

/*
The factor by which weights are adjusted; either to fit 16 bit values or to
reduce bias.
*/
#define WEIGHT_SCALE 9


#define SYMBOL_EMPTY          EMPTY_STONE_CHAR
#define SYMBOL_OWN_STONE      BLACK_STONE_CHAR
#define SYMBOL_OWN_OR_EMPTY   BLACK_STONE_CHAR_ALT
#define SYMBOL_OPT_STONE      WHITE_STONE_CHAR
#define SYMBOL_OPT_OR_EMPTY   WHITE_STONE_CHAR_ALT
#define SYMBOL_STONE_OR_EMPTY '?'
#define SYMBOL_BORDER         '-'



typedef struct __pat3_{
	u16 value;
	u16 weight;
	struct __pat3_ * next;
} pat3;



/*
Lookup of pattern value for the specified player.
RETURNS pattern weight or 0 if not found
*/
u16 pat3_find(
    u16 value,
    bool is_black
);

/*
Rotate and flip the pattern to its unique representative.
Avoid using, is not optimized.
*/
void pat3_reduce_auto(
    u8 v[3][3]
);

/*
Transposes part of an input matrix board into a 3x3 matrix pattern codified,
with board safety.
*/
void pat3_transpose(
    u8 dst[3][3],
    const u8 p[TOTAL_BOARD_SIZ],
    move m
);

/*
Codifies the pattern in a 16 bit unsigned value.
*/
u16 pat3_to_string(
    const u8 p[3][3]
);

/*
Decodes a 16-bit value into a 3x3 pattern, with empty center.
*/
void string_to_pat3(
    u8 dst[3][3],
    u16 src
);

/*
Invert stone colors.
*/
void pat3_invert(
    u8 p[3][3]
);

/*
Reads a .pat3 patterns file and expands all patterns into all possible and
patternable configurations.
*/
void pat3_init();


#endif
