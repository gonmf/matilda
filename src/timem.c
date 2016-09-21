/*
Generic time-keeping functions and Go time system related functions. All times
are in milliseconds.
*/

#include "matilda.h"

#include <time.h>
#include <unistd.h>
#include <stdio.h>

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#include "alloc.h"
#include "types.h"


/*
Returns a current time mark with millisecond precision. Will be monotonic if
supported by the system. Is thread-safe.
RETURNS time in milliseconds
*/
u64 current_time_in_millis()
{
    struct timespec ts;

#ifdef __MACH__
    /*
    macOS
    */
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    ts.tv_sec = mts.tv_sec;
    ts.tv_nsec = mts.tv_nsec;

#else
    /*
    Linux, BSD
    */
    /* POSIX.1 conforming */
#ifdef _POSIX_MONOTONIC_CLOCK
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
#endif

    u64 ret = ts.tv_sec * 1000;
    ret += ts.tv_nsec / 1000000;
    return ret;
}

/*
Returns the current nanoseconds count from the system clock. Is not monotonic.
RETURNS nanoseconds
*/
u64 current_nanoseconds()
{
    struct timespec ts;

#ifdef __MACH__
    /*
    macOS
    */
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    ts.tv_sec = mts.tv_sec;
    ts.tv_nsec = mts.tv_nsec;

#else
    /*
    Linux, BSD
    */
    /* POSIX.1 conforming */
#ifdef _POSIX_MONOTONIC_CLOCK
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
#endif

    return ts.tv_nsec;
}

/*
Produces a textual timestamp based on the local timezone and system time.
*/
void timestamp(
    char * buffer
){
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    snprintf(buffer, MAX_PAGE_SIZ, "%02u:%02u:%02u", tm.tm_hour, tm.tm_min,
        tm.tm_sec);
}
