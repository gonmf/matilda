/*
Support for rotating buffers, to avoid dynamic allocating.

Use these buffers to pass data around and writing logs, places where it doesn't
have to persist for long.
*/

#ifndef MATILDA_BUFFER_H
#define MATILDA_BUFFER_H

#include "matilda.h"

#include "types.h"

#define NR_OF_BUFFERS 8

/*
Get a rotating buffer of MAX_PAGE_SIZ size.
*/
void * get_buffer();

#endif
