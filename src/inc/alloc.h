/*
Guard functions over standard malloc.
*/

#ifndef MATILDA_ALLOC_H
#define MATILDA_ALLOC_H

#include "matilda.h"

/*
Initiate the safe allocation.
*/
void alloc_init();

/*
Allocate a small block of memory; intended for string formatting and the like.
*/
void * alloc();

/*
Releases a previously allocated block of memory.
*/
void release(void * ptr);

#endif
