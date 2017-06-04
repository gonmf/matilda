/*
Functions related to reading and writing SGF FF[4] files.
http://www.red-bean.com/sgf/

Play variations and annotations/commentary are ignored.
*/

#ifndef MATILDA_SGF_H
#define MATILDA_SGF_H

#include "matilda.h"

#include "types.h"
#include "board.h"
#include "game_record.h"



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
    char filename[MAX_PAGE_SIZ]
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
Reset the printing of warning messages. Since the process of processing SGF
files can be repetitive a lot of noise could be produced. By default warning
messages are only shown the first time a particular type of problem is found
with the files. Use this function to reset the warnings to be shown.
*/
void reset_warning_messages();

/*
RETURNS true if the game has been found and read correctly
*/
bool import_game_from_sgf(
    game_record * gr,
    const char * filename
);

/*
Import a game record from the contents of the buffer.
RETURNS true if the game has been found and read correctly
*/
bool import_game_from_sgf2(
    game_record * gr,
    const char * filename,
    char * buf,
    u32 buf_siz
);

#endif
