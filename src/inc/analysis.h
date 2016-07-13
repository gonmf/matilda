/*
Functions for human-like analysis and ponderation on the game.

This is very incomplete so far.
*/

#ifndef MATILDA_ANALYSIS_H
#define MATILDA_ANALYSIS_H

#include "matilda.h"

#include <stdio.h>

#include "types.h"
#include "board.h"

/*
Produces a textual opinion on the best followup, given the time available to
think.
*/
void request_opinion(
    char * buf,
    const board * b,
    bool is_black,
    u64 milliseconds
);

#endif
