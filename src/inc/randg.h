/*
Non-cryptographic random number generation functions

Reminder: maximums are exclusive for integer functions and inclusive (and very
unlikely) for floating point functions.
*/


#ifndef MATILDA_RANDG_H
#define MATILDA_RANDG_H

#include "config.h"

#include "types.h"


/*
Initiate the seeds for the different thread RNG, again.
*/
void rand_reinit();

/*
Initiate the seeds for the different thread RNG.
*/
void rand_init();

/*
Fast and well distributed 16-bit RNG based on the glibc mixed LCG.
RETURNS pseudo random 16-bit number
*/
u16 rand_u16(
    u16 max /* exclusive */
);

/*
Slow alternative for 32-bit. Avoid using.
RETURNS pseudo random 32-bit number
*/
u32 rand_u32(
    u32 max /* exclusive */
);

/*
Fast floating pointer random number generator.
RETURNS pseudo random IEEE 754 floating point
*/
float rand_float(
    float max /* inclusive */
);

#endif
