/*
Go-specific time system functions, on top of a time_settings structure. The
timed_out field is used to indicate the player must have lost on time -- this
does not necessarily interrupt the match, if the time keeping referee doesn't
say anything. All times are in milliseconds.
*/

#ifndef MATILDA_TIME_CTRL_H
#define MATILDA_TIME_CTRL_H

#include "matilda.h"

#include "types.h"

/*
If disabled then latency compensation falls back to the value set in the
constant DETECT_NETWORK_LATENCY.
*/
#define DETECT_NETWORK_LATENCY false


/*
How much time a play should be given over the linear distribution of time for
the match. Values over 1 favor thinking more in the begining of matches, which
is the objective.
*/
#if BOARD_SIZ < 12
#define TIME_ALLOT_FACTOR 1.24 // TODO
#else
#define TIME_ALLOT_FACTOR 1.24 // TODO
#endif

#define EXPECTED_GAME_LENGTH ((TOTAL_BOARD_SIZ * 2) / 3)


typedef struct __time_system_ {
	bool can_timeout;
	bool timed_out;

	u32 main_time;
	u32 byo_yomi_stones;
	u32 byo_yomi_time;
	u32 byo_yomi_periods;

	u32 main_time_remaining;
	u32 byo_yomi_stones_remaining;
	u32 byo_yomi_time_remaining;
	u32 byo_yomi_periods_remaining;
} time_system;



/*
Calculate the time available based on a Canadian byo-yomi time system. Also
compensates for network latency.
RETURNS time available in milliseconds
*/
u32 calc_time_to_play(
    time_system * ts,
    u16 turns_played
);

/*
Set the complete Canadian byo-yomi time system.
*/
void set_time_system(
    time_system * ts,
    u32 main_time,
    u32 byo_yomi_time,
    u32 byo_yomi_stones,
    u32 byo_yomi_periods
);

/*
Set the time system based only on absolute time (sudden death).
*/
void set_sudden_death(
    time_system * ts,
    u32 main_time
);

/*
Set the time system based on a constant time per turn.
*/
void set_time_per_turn(
    time_system * ts,
    u32 time_per_turn
);

/*
Advance the clock, consuming the available time, byo-yomi stones and possibly
affecting the value indicating time out.
*/
void advance_clock(
    time_system * ts,
    u32 milliseconds
);

/*
Reset the clock to the initial values of the system.
*/
void reset_clock(
    time_system * ts
);

/*
Convert a time system into a textual description. Composite overtime format is
used, or the word infinite.
*/
void time_system_to_str(
    char * dst,
    time_system * ts
);

/*
Convert a string in the format time+numberxtime/number to a time system struct.
RETURNS true if successful and value stored in dst
*/
bool str_to_time_system(
    time_system * dst,
    const char * src
);

#endif
