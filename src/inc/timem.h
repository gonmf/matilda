/*
Generic time-keeping functions and Go time system related functions. All times
are in milliseconds.
*/

#ifndef MATILDA_TIME_M_H
#define MATILDA_TIME_M_H

#include "matilda.h"

#include "types.h"


/*
Returns a current time mark with millisecond precision. Will be monotonic if
supported by the system. Is thread-safe.
RETURNS time in milliseconds
*/
u64 current_time_in_millis();

/*
Returns the current nanoseconds count from the system clock. Is not monotonic.
RETURNS nanoseconds
*/
u64 current_nanoseconds();

/*
Returns textual timestamp. It is not dynammically allocated and the memory will
be reused for the next timestamp (but is thread safe).
RETURNS timestamp
*/
const char * timestamp();


#endif
