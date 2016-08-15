/*
Go-specific time system functions, on top of a time_settings structure. The
timed_out field is used to indicate the player must have lost on time -- this
does not necessarily interrupt the match, if the time keeping referee doesn't
say anything. All times are in milliseconds.
*/

#include "matilda.h"

#include <string.h>

#include "alloc.h"
#include "board.h"
#include "stringm.h"
#include "time_ctrl.h"
#include "types.h"

u32 network_roundtrip_delay = LATENCY_COMPENSATION;
bool network_round_trip_set = false;

double time_allot_factor = TIME_ALLOT_FACTOR;


/*
Calculate the time available based on a Canadian byo-yomi time system. Also
compensates for network latency.
RETURNS time available in milliseconds
*/
u32 calc_time_to_play(
    time_system * ts,
    u16 turns_played
){
#if 0
    /*
TODO testing for paper
    */
    d32 turns_played2 = turns_played;
    double e1 = (double)(EXPECTED_GAME_LENGTH - turns_played2);
    double turns_left = MAX(e1, 4.0);
    return  ts->main_time_remaining / turns_left;







#else






    double e1 = EXPECTED_GAME_LENGTH - turns_played;
    double turns_left = MAX(e1, BOARD_SIZ) / 2.0;
    double mtt = ts->main_time_remaining / turns_left;
    mtt *= time_allot_factor;

    double t_t;
    if(ts->byo_yomi_stones_remaining > 0)
    {
        double byt = ts->byo_yomi_time_remaining /
            ((double)ts->byo_yomi_stones_remaining);
        t_t = MAX(byt, mtt);
    }
    else
    {
        t_t = mtt;
    }

    /*
    Non-linear factor
    */

    /*
    Network lag correction
    */
#if DETECT_NETWORK_LATENCY
    if(network_round_trip_set && t_t > network_roundtrip_delay)
        t_t -= network_roundtrip_delay;
#else
    if(t_t > LATENCY_COMPENSATION)
        t_t -= LATENCY_COMPENSATION;
#endif

    return t_t;


#endif
}

/*
Set the complete Canadian byo-yomi time system.
*/
void set_time_system(
    time_system * ts,
    u32 main_time,
    u32 byo_yomi_time,
    u32 byo_yomi_stones,
    u32 byo_yomi_periods
){
    ts->can_timeout = true;
    ts->timed_out = false;
    ts->main_time = ts->main_time_remaining = main_time;
    ts->byo_yomi_time = ts->byo_yomi_time_remaining = byo_yomi_time;
    ts->byo_yomi_stones = ts->byo_yomi_stones_remaining = byo_yomi_stones;
    ts->byo_yomi_periods = ts->byo_yomi_periods_remaining = byo_yomi_periods;
}

/*
Set the time system based only on absolute time (sudden death).
*/
void set_sudden_death(
    time_system * ts,
    u32 main_time
){
    ts->can_timeout = true;
    ts->timed_out = false;
    ts->main_time = ts->main_time_remaining = main_time;
    ts->byo_yomi_time = ts->byo_yomi_time_remaining = 0;
    ts->byo_yomi_stones = ts->byo_yomi_stones_remaining = 0;
    ts->byo_yomi_periods = ts->byo_yomi_periods_remaining = 0;
}

/*
Set the time system based on a constant time per turn.
*/
void set_time_per_turn(
    time_system * ts,
    u32 time_per_turn
){
    ts->can_timeout = false;
    ts->timed_out = false;
    ts->main_time = ts->main_time_remaining = 0;
    ts->byo_yomi_time = ts->byo_yomi_time_remaining = time_per_turn;
    ts->byo_yomi_stones = ts->byo_yomi_stones_remaining = 1;
    ts->byo_yomi_periods = ts->byo_yomi_periods_remaining = 1;
}

/*
Advance the clock, consuming the available time, byo-yomi stones and possibly
affecting the value indicating time out.
*/
void advance_clock(
    time_system * ts,
    u32 milliseconds
){
    if(!ts->can_timeout || ts->timed_out)
        return;

    bool consumed_byo_yomi_stone = false;

    while(milliseconds > 0)
    {
        if(ts->main_time_remaining == 0)
        {
            /*
            Byo-yomi period
            */
            u32 byo_time_elapsed = MIN(ts->byo_yomi_time_remaining,
                milliseconds);
            ts->byo_yomi_time_remaining -= byo_time_elapsed;
            milliseconds -= byo_time_elapsed;

            if(consumed_byo_yomi_stone == false)
            {
                ts->byo_yomi_stones_remaining--;
                consumed_byo_yomi_stone = true;
            }

            if(ts->byo_yomi_time_remaining == 0)
            {
                /*
                The period time has run out, consume a period.
                */
                ts->byo_yomi_periods_remaining--;
                if(ts->byo_yomi_periods_remaining == 0)
                {
                    ts->timed_out = true;
                    return;
                }
                else
                {
                    /*
                    Set the time available for the new period.
                    */
                    ts->byo_yomi_stones_remaining = ts->byo_yomi_stones;
                    ts->byo_yomi_time_remaining = ts->byo_yomi_time;
                }

            }
            else
            {
                if(ts->byo_yomi_stones_remaining == 0)
                {
                    /*
                    The period time has not run out and we have played all the
                    stones; reset the period time.
                    */
                    ts->byo_yomi_stones_remaining = ts->byo_yomi_stones;
                    ts->byo_yomi_time_remaining = ts->byo_yomi_time;
                }
            }
        }
        else
        {
            /*
            Absolute period
            */
            u32 main_time_elapsed = MIN(ts->main_time_remaining, milliseconds);
            ts->main_time_remaining -= main_time_elapsed;
            milliseconds -= main_time_elapsed;
        }
    }
}

/*
Reset the clock to the initial values of the system.
*/
void reset_clock(
    time_system * ts
){
    ts->timed_out = false;
    ts->main_time_remaining = ts->main_time;
    ts->byo_yomi_time_remaining = ts->byo_yomi_time;
    ts->byo_yomi_stones_remaining = ts->byo_yomi_stones;
    ts->byo_yomi_periods_remaining = ts->byo_yomi_periods;
}

/*
Convert a time system into a textual description.
*/
void time_system_to_str(
    char * dst,
    time_system * ts
){
    char * a;
    u32 abs_time = ts->main_time;
    if(abs_time == 0)
        a = "";
    else
        a = "ms";
    if(abs_time >= 1000 && (abs_time % 1000) == 0)
    {
        a = "s";
        abs_time /= 1000;

        if(abs_time >= 60 && (abs_time % 60) == 0)
        {
            a = "m";
            abs_time /= 60;
        }

        if(abs_time >= 60 && (abs_time % 60) == 0)
        {
            a = "h";
            abs_time /= 60;
        }
    }

    char * b;
    u32 byo_time = ts->byo_yomi_time;
    if(byo_time == 0)
        b = "";
    else
        b = "ms";
    if(byo_time >= 1000 && (byo_time % 1000) == 0)
    {
        b = "s";
        byo_time /= 1000;

        if(byo_time >= 60 && (byo_time % 60) == 0)
        {
            b = "m";
            byo_time /= 60;
        }

        if(byo_time >= 60 && (byo_time % 60) == 0)
        {
            b = "h";
            byo_time /= 60;
        }
    }

    snprintf(dst, MAX_PAGE_SIZ, "%u%s+%ux%u%s/%u", abs_time, a,
        ts->byo_yomi_periods, byo_time, b, ts->byo_yomi_stones);
}

static d32 str_to_milliseconds(const char * s){
    char * char_idx = strchr(s, 'm');
    d32 mul = 0; /* multiplier */
    if(char_idx != NULL)
    {
        if(char_idx[1] == 's') /* milliseconds */
        {
            mul = 1;
        }
        else /* minutes */
        {
            mul = 1000 * 60;
        }
    }
    else
    {
        char_idx = strchr(s, 's');
        if(char_idx != NULL)
        {
            mul = 1000;
        }
        else
        {
            char_idx = strchr(s, 'h');
            if(char_idx != NULL)
            {
                mul = 1000 * 60 * 60;
            }
        }
    }

    if(mul == 0)
    {
        if(strcmp(s, "0") == 0)
            return 0;
        return -1; /* error */
    }

    d32 ret = 0;
    for(u8 i = 0; s[i]; ++i)
    {
        if(s[i] < '0' || s[i] > '9')
            break;
        ret = ret * 10 + (s[i] - '0');
    }

    if(ret <= 0)
        return -1;

    return ret * mul;
}

/*
Convert a string in the format time+numberxtime/number to a time system struct.
RETURNS true if successful and value stored in dst
*/
bool str_to_time_system(
    time_system * dst,
    const char * src
){
    if(src == NULL)
        return false;

    u32 len = strlen(src);
    if(len < 7)
        return false;

    char * s = alloc();
    if(s == NULL)
        return false;
    char * original_ptr = s;

    memcpy(s, src, len + 1);
    s = trim(s);
    len = strlen(s);
    if(len < 9)
    {
        release(original_ptr);
        return false;
    }

    /*
    time + ...
    */
    char * char_idx = strchr(s, '+');
    if(char_idx == NULL)
    {
        release(original_ptr);
        return false;
    }
    char_idx[0] = 0;
    char * rest = char_idx + 1;

    d32 t = str_to_milliseconds(s);
    if(t < 0)
    {
        release(original_ptr);
        return false;
    }
    u32 absolute_milliseconds = t;
    s = rest;

    /*
    ... + number x ...
    */
    char_idx = strchr(s, 'x');
    if(char_idx == NULL)
    {
        release(original_ptr);
        return false;
    }
    char_idx[0] = 0;
    rest = char_idx + 1;

    if(!parse_int(s, &t) || t < 0)
    {
        release(original_ptr);
        return false;
    }
    u32 byoyomi_periods = t;
    s = rest;

    /*
    ... x time / ...
    */
    char_idx = strchr(s, '/');
    if(char_idx == NULL)
    {
        release(original_ptr);
        return false;
    }
    char_idx[0] = 0;
    rest = char_idx + 1;

    t = str_to_milliseconds(s);
    if(t < 0 || (t == 0 && byoyomi_periods > 0))
    {
        release(original_ptr);
        return false;
    }
    u32 byoyomi_milliseconds = t;
    s = rest;

    /*
    ... / number
    */
    if(!parse_int(s, &t) || (t < 0) || (t == 0 && byoyomi_periods > 0))
    {
        release(original_ptr);
        return false;
    }
    u32 byoyomi_stones = t;

    if(absolute_milliseconds == 0 && byoyomi_milliseconds == 0)
    {
        release(original_ptr);
        return false;
    }

    dst->main_time = absolute_milliseconds;
    dst->byo_yomi_stones = byoyomi_stones;
    dst->byo_yomi_time = byoyomi_milliseconds;
    dst->byo_yomi_periods = byoyomi_periods;

    release(original_ptr);
    return true;
}
