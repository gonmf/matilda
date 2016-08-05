/*
Functions related to reading and writing SGF FF[4] files.
http://www.red-bean.com/sgf/

Play variations are not supported.
*/

#ifndef MATILDA_SGF_H
#define MATILDA_SGF_H

#include "matilda.h"

#include "types.h"
#include "board.h"
#include "game_record.h"



/*
Reads the header information of an SGF file.
RETURNS true if possibly valid
*/
bool sgf_info(
    const char * sgf_buf,
    bool * black_won,
    bool * chinese_rules,
    bool * japanese_rules,
    bool * normal_komi
);

/*
Reads the sequence of plays from SGF text.
If plays happen out of order they count as a pass by the other player.
RETURNS number of plays found or -1 on format error
*/
d16 sgf_to_boards(
    char * sgf_buf,
    move * plays,
    bool * irregular_play_order
);

/*
Writes a game record to a buffer of size length, to the best of the available
information.
RETURNS chars written
*/
u32 export_game_as_sgf_to_buffer(
    const game_record * gr,
    char * buffer,
    u32 size
);

/*
Writes a game record to SGF file with automatically generated name, to the best
of the available information.
Writes the file name generated to the buffer provided.
RETURNS false on error
*/
bool export_game_as_sgf_auto_named(
    const game_record * gr,
    char filename[32]
);

/*
Writes a game record to SGF file, to the best of the available information.
Fails if the file already exists.
RETURNS false on error
*/
bool export_game_as_sgf(
    const game_record * gr,
    const char * filename
);

/*
RETURNS true if the game has been found and read correctly
*/
bool import_game_from_sgf(
    game_record * gr,
    const char * filename
);


#endif
