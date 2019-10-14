/*
Prime number generator

Intended for initializing hash tables
*/

#include "config.h"

#include "types.h"

static bool primality_test(
    u32 v
) {
    if (v < 2) {
        return false;
    }

    if (v < 4) {
        return true;
    }

    if ((v % 2) == 0) {
        return false;
    }

    for (u32 d = 3; d * d <= v; d += 2) {
        if ((v % d) == 0) {
            return false;
        }
    }

    return true;
}

/*
Obtains the closest prime equal or above value v.
RETURNS prime value
*/
u32 get_prime_near(
    u32 v
) {
    while (!primality_test(v)) {
        ++v;
    }

    return v;
}
