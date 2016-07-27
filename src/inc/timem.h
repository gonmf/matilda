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
Produces a textual timestamp based on the local timezone and system time.
RETURNS timestamp
*/
const char * timestamp();


#endif
