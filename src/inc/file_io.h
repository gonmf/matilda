/*
Functions for file input/output.
*/

#ifndef MATILDA_FILE_IO_H
#define MATILDA_FILE_IO_H

#include "matilda.h"

#include <stdio.h>

#include "types.h"

/*
RETURNS the number of bytes read or -1 if failed to open/be read
*/
d32 read_binary_file(
    const char * filename,
    void * dst_buf,
    u32 buf_len
);

/*
RETURNS the number of ASCII characters read or -1 if failed to open/be read
*/
d32 read_ascii_file(
    const char * filename,
    char * dst_buf,
    u32 buf_len
);

/*
Searches for and allocates the space needed for the relative path to the files
found ending with the text present in extension.
At most fills max_files file names.
RETURN number of file names saved
*/
u32 recurse_find_files(
    const char * root,
    const char * extension,
    char ** filenames,
    u32 max_files
);

#endif
