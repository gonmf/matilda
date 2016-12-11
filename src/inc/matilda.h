#ifndef MATILDA_ROOT_H
#define MATILDA_ROOT_H

/*******************************************************************************
                                   EDIT ME
********************************************************************************
Edit this section. 1 and 0 mean yes and no, respectively.
If you do not understand the significance of a setting, consider leaving it with
the default value.



Board/goban size given by the length of one side.

EXPECTED: 5, 7, 9, 11, 13, 15, 17, 19 or 21
*/
#define BOARD_SIZ 9

/*
Default komidashi used, multiplied by 2 to give an integer number.
The komi is not reset between matches if changed via GTP.
Example: 15 for 7.5; 11 for 5.5

EXPECTED: 0 to 15
*/
#if BOARD_SIZ < 10
#define DEFAULT_KOMI 14 /* 7.0 komi */
#else
#define DEFAULT_KOMI 15 /* 7.5 komi */
#endif

/*
Default memory available for use by transposition tables, in MiB.
Take note that the real total memory used will be a few MiB more.

EXPECTED: 10 to 64000
*/
#define DEFAULT_UCT_MEMORY 7000


/*
When playing online the communication can suffer a small latency, which can
negatively impact the game time control and cause Matilda to run out of time.
If this is the case you can set a constant latency compensation so that it
thinks less per turn, but doesn't timeout.
The value is in milliseconds.

EXPECTED: 2 to 400
*/
#define LATENCY_COMPENSATION 0 // 250 /* Japan is quite far away */


/*
Data folder. This folder needs to be found and contain at least a Zobrist
codification table and handicaps for the board size in use.
This can also be changed at startup with the flag -data.

EXPECTED: relative or absolute path ending with / character
*/
#define DEFAULT_DATA_PATH "./data/"

/*
Set whether to build for release: without C assertions and other tests.
A debug build suffers a heavy performance penalty.

EXPECTED: 0 or 1
*/
#define MATILDA_RELEASE_MODE 1


/*
Select the number of threads to be used by OpenMP.
The value 0 means automatic, which should be equal to the number of real cores
plus hyperthreaded.

EXPECTED: 0 to 64
*/
#define DEFAULT_NUM_THREADS 0


/*
Hard limit on number of threads. This is used just for initialization, not for
limiting dynamic number of OpenMP threads (which, by the way, are disabled).
Just set a very high number.

EXPECTED: 1 to 64
*/
#define MAXIMUM_NUM_THREADS 8


/*
Set how many visits are needed before expanding a new state in MCTS. If the
program is running out of memory mid-turn consider increasing this value.
Tuned with CLOP in 9x9 with 10k playouts/turn in self-play for 34k games.

EXPECTED: 0 to 10
*/
#define UCT_EXPANSION_DELAY 5


/*
Set the default time, in milliseconds, to be used to think per turn.
This is used when no time control system is set.
If a system is set and it allows infinite time to think, Matilda will think
until running out of memory.
EXPECTED: 1000 to 100000
*/
#define DEFAULT_TIME_PER_TURN 1000


/*******************************************************************************
                                    END
*******************************************************************************/























#define TOTAL_BOARD_SIZ (BOARD_SIZ * BOARD_SIZ)

/*
Preprocessor stringification of board size.
*/
#define BOARD_SIZ_AS_STR _BOARD_SIZ_AS_STR(BOARD_SIZ)
#define _BOARD_SIZ_AS_STR(a) __BOARD_SIZ_AS_STR(a)
#define __BOARD_SIZ_AS_STR(a) #a


/*
Compliancy with POSIX.1 aka IEEE Std. 1003.1b-1993 is required for the
functions: fdopen, strtok_r, rand_r, fsync
*/
#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 600


#define YN(EXPR) ((EXPR) ? "yes" : "no")

#define TF(EXPR) ((EXPR) ? "true" : "false")

#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))

#define MIN(X,Y) ((X) > (Y) ? (Y) : (X))





#if MATILDA_RELEASE_MODE
#define NDEBUG
#endif

#if DEFAULT_UCT_MEMORY < 2
#error Error: insufficient MCTS UCT memory (minimum 2MiB).
#endif

#if BOARD_SIZ < 5
#error Error: board size is too small.
#endif

/* Boards above 21 make it impossible to use at most 1 byte for liberties. */
#if BOARD_SIZ > 21
#error Error: board size is too big.
#endif

#if (BOARD_SIZ % 2) == 0
#error Error: board side cannot be even.
#endif

#if UCT_EXPANSION_DELAY < 0
#error Error: illegal UCT expansion delay value.
#endif

#if MAXIMUM_NUM_THREADS < 1
#error Error: illegal maximum number of threads (< 1).
#endif

#endif


