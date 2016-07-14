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
#define BOARD_SIZ 19

/*
Default komidashi used, multiplied by 2 to give an integer number.
The komi is not reset between matches if changed via GTP.
Example: 15 for 7.5; 11 for 5.5

EXPECTED: 0 to 15
*/
#define DEFAULT_KOMI 15


/*
Default memory available for use by transposition tables, in MiB.
Take not that the real total memory used will be a few MiB more.

EXPECTED: 10 to 64000
*/
#define DEFAULT_UCT_MEMORY 7000


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
Set whether the program should resign or pass, when losing hard.

EXPECTED: 0 or 1
*/
#define CAN_RESIGN 1


/*
Change these two lines to ignore time control systems and use a fixed number of
playouts per turn in the MCTS-UCT RAVE algorithm.
MCTS early stopping is also deactivated, even if memory runs out.

EXPECTED: 0 or 1
EXPECTED: 100 to 1000000000
*/
#define LIMIT_BY_PLAYOUTS 1
#define PLAYOUTS_PER_TURN 10000


/*
Set how many visits are needed before expanding a new state in MCTS. If the
program is running out of memory mid-turn consider increasing this value.

EXPECTED: 0 to 10
*/
#define UCT_EXPANSION_DELAY 0


/*
Set the default time in milliseconds to be used to think per turn.
This is used when no time system is set.
EXPECTED: 1000 to 100000
*/
#define DEFAULT_TIME_PER_TURN 1000


/*
Enable Frisbee Go variant. Hurts performance by disabling use of LGRF1. When
enabled the frisbee accuracy can be overriden via GTP or program arguments.
Frisbee accuracy is given in integer percentage.

EXPECTED: 0 or 1
EXPECTED: 1 to 100
*/
#define ENABLE_FRISBEE_GO 0
#define DEFAULT_FRISBEE_ACCURACY 50 /* in 100 */



/*******************************************************************************
                                    END
*******************************************************************************/











/*
There is not set-in-stone criteria for the version numbers, but the major
version can be changed when a very large rewrite or expansion has happened that
changed either the strength of the program in a big amount or the external files
as they are understood. The minor mark can be changed in any non-decreasing way.
It is reset when the major mark is increased.
*/
#define VERSION_MAJOR 1
#define VERSION_MINOR 11





/*
Stringification of board size.
*/
#define BOARD_SIZ_AS_STR _BOARD_SIZ_AS_STR(BOARD_SIZ)
#define _BOARD_SIZ_AS_STR(a) __BOARD_SIZ_AS_STR(a)
#define __BOARD_SIZ_AS_STR(a) #a


/*
Compliancy with POSIX.1 aka IEEE Std. 1003.1b-1993 is required for the
functions: clock_gettime, fdopen, strtok_r, rand_r, fsync
*/
#define _POSIX_C_SOURCE 199309L

/*
This is required for fsync in glibc versions < 2.8 and for mkstemps
*/
#define _BSD_SOURCE

/*
This is required for clock_getcpuclockid
*/
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

#if LIMIT_BY_PLAYOUTS != 0 && PLAYOUTS_PER_TURN < 1
#error Error: illegal number of playouts per turn (< 1).
#endif

#if LIMIT_BY_PLAYOUTS == 0 && DEFAULT_TIME_PER_TURN < 10
#error Error: illegal time available per turn (< 10ms).
#endif

#if MAXIMUM_NUM_THREADS < 1
#error Error: illegal maximum number of threads (< 1).
#endif

#if ENABLE_FRISBEE_GO
#if DEFAULT_FRISBEE_ACCURACY < 1
#error Error: illegal Frisbee go accuracy (< 1%).
#endif

#if DEFAULT_FRISBEE_ACCURACY > 100
#error Error: illegal Frisbee go accuracy (> 100%).
#endif
#endif

#endif


