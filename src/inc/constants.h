/*
Initialization of game wide constants based on board size.

To use declare as external:
d16 komi;
u8 out_neighbors8[TOTAL_BOARD_SIZ];
u8 out_neighbors4[TOTAL_BOARD_SIZ];
move_seq neighbors_side[TOTAL_BOARD_SIZ];
move_seq neighbors_diag[TOTAL_BOARD_SIZ];
move_seq neighbors_3x3[TOTAL_BOARD_SIZ];
bool border_left[TOTAL_BOARD_SIZ];
bool border_right[TOTAL_BOARD_SIZ];
bool border_top[TOTAL_BOARD_SIZ];
bool border_bottom[TOTAL_BOARD_SIZ];
u8 distances_to_border[TOTAL_BOARD_SIZ];
move_seq nei_dst_3[TOTAL_BOARD_SIZ];
move_seq nei_dst_4[TOTAL_BOARD_SIZ];
u8 active_bits_in_byte[256];
*/

#ifndef MATILDA_CONSTANTS_H
#define MATILDA_CONSTANTS_H

#include "config.h"


/*
Initialize a series of constants based on the board size in use.
*/
void board_constants_init();

#endif
