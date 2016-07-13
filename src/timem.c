/*
Generic time-keeping functions and Go time system related functions. All times
are in milliseconds.
*/

#include "matilda.h"

#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <omp.h>

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

#include "types.h"
#include "buffer.h"


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
    Mac OSX
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
    Mac OSX
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


static u64 ts_time = 0;

/*
Returns textual timestamp. It is not dynammically allocated and the memory will
be reused for the next timestamp (but is thread safe).
RETURNS timestamp
*/
const char * timestamp()
{
    char * buf = get_buffer();

    if(ts_time == 0)
    {
        snprintf(buf, 64, "0.000");
        ts_time = current_time_in_millis();
        return buf;
    }

    u64 dif = current_time_in_millis() - ts_time;
    u64 days = dif / (24 * 60 * 60 * 1000);
    dif %= (24 * 60 * 60 * 1000);
    u64 hours = dif / (60 * 60 * 1000);
    dif %= (60 * 60 * 1000);
    u64 minutes = dif / (60 * 1000);
    dif %= (60 * 1000);
    u64 seconds = dif / (1000);
    dif %= (1000);

    if(days > 0)
     snprintf(buf, 64, "%" PRIu64 "d%" PRIu64 "h%" \
        PRIu64 "m", days, hours, minutes);
    else
        if(hours > 0)
            snprintf(buf, 64, "%" PRIu64 "h%" \
                PRIu64 "m%" PRIu64 "s", hours, minutes, seconds);
        else
            if(minutes > 0)
                snprintf(buf, 64, "%" PRIu64 "m%" \
                    PRIu64 ".%03" PRIu64 "", minutes, seconds, dif);
            else
                snprintf(buf, 64, "%" PRIu64 ".%03" \
                    PRIu64 "", seconds, dif);
    return buf;
}
