/*
Prime number generator

Intended for initializing hash tables
*/


#ifndef MATILDA_PRIMES_H
#define MATILDA_PRIMES_H

#include "matilda.h"

#include "types.h"

/*
Obtains the closest prime equal or above value v.
RETURNS prime value
*/
u32 get_prime_near(
    u32 v
);

#endif
