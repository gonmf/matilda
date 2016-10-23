/*
Fast memory allocation layer above standard malloc.

These are meant to inexpensively allocate buffers for string operations.

They are thread-safe, fast, with canary values used (in debug mode) to ensure
memory is correctly freed and written to.

If you are need to perform recursive operations then use malloc/free. Releasing
these buffers does not actually free the underlying memory to be used by other
programs.
*/

#ifndef MATILDA_ALLOC_H
#define MATILDA_ALLOC_H

#include "matilda.h"

/*
Initiate the safe allocation functions.
*/
void alloc_init();

/*
Allocate a block of exactly MAX_PAGE_SIZ.
Thread-safe.
*/
void * alloc();

/*
Releases a previously allocated block of memory, to be used again in later
calls. Does not free the memory.
Thread-safe.
*/
void release(void * ptr);

#endif
