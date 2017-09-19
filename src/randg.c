/*
Non-cryptographic random number generation functions

Reminder: maximums are exclusive for integer functions and inclusive (and very
unlikely) for floating point functions.
*/

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <omp.h>

#include "alloc.h"
#include "flog.h"
#include "timem.h"
#include "types.h"

static u32 state[MAXIMUM_NUM_THREADS];
static bool rand_inited = false;

/*
Initiate the seeds for the different thread RNG, again.
*/
void rand_reinit()
{
    char * buf = alloc();

    u16 idx = 0;
    idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "RNG seed vector:\n");

    for(u16 i = 0; i < MAXIMUM_NUM_THREADS; )
    {
        state[i] = (u32)current_nanoseconds();
        bool found = false;
        for(u16 j = 0; j < i; ++j)
            if(state[j] == state[i])
            {
                found = true;
                break;
            }

        if(!found && state[i] > 0)
            ++i;
    }

    for(u16 i = 0; i < MAXIMUM_NUM_THREADS; ++i)
        idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "%u: %x\n", i, state[i]);

    flog_debug("rand", buf);
    release(buf);

    rand_inited = true;
}

/*
Initiate the seeds for the different thread RNG.
*/
void rand_init()
{
    if(!rand_inited)
    {
        alloc_init();
        rand_reinit();
    }
}

/*
Fast and well distributed 16-bit RNG based on the glibc mixed LCG.
RETURNS pseudo random 16-bit number
*/
u16 rand_u16(
    u16 max /* exclusive */
){
    u32 s = state[omp_get_thread_num()];
    state[omp_get_thread_num()] = ((s * 1103515245) + 12345) & 0x7fffffff;
    return ((s & 0xffff) * ((u32)max)) >> 16;
}

/*
Slow alternative for 32-bit. Avoid using.
RETURNS pseudo random 32-bit number
*/
u32 rand_u32(
    u32 max /* exclusive */
){
    double gen = (double)rand_r(&state[omp_get_thread_num()]);
    return (gen * ((double)max)) / ((double)RAND_MAX);
}

/*
Fast floating pointer random number generator.
RETURNS pseudo random IEEE 754 floating point
*/
float rand_float(
    float max /* inclusive */
){
    /*
    Adapted from the method used in Pachi (GPLv2), which is itself based on the
    method described in www.rgba.org/articles/sfrand/sfrand.htm (proprietary
    intelectual property). If you believe this may constitue a violation of
    someones copyright please contact me immediatly (contact information
    available in AUTHORS file attached).
    */
    u32 s = state[omp_get_thread_num()];
    s *= 16807;
    state[omp_get_thread_num()] = s;
    union { u32 ul; float f; } p;
    p.ul = ((s & 0x007fffff) - 1) | 0x3f800000;
    float f = p.f - 1.0f;
    return f * max;
}
