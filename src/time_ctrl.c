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
    if(ts->byo_yomi_time > 0 && ts->byo_yomi_stones == 0)
        return UINT32_MAX;

    double e1 = EXPECTED_GAME_LENGTH - turns_played;
    double turns_left = MAX(e1 / 2.0, (double)BOARD_SIZ);
    double mtt = ts->main_time_remaining / turns_left;

    /*
    Non-linear factor
    */
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
    Network lag correction
    */
#if DETECT_NETWORK_LATENCY
    if(network_round_trip_set && t_t > network_roundtrip_delay)
        t_t -= network_roundtrip_delay;
#else
    t_t -= LATENCY_COMPENSATION;
#endif

    return (u32)MAX(t_t, 100.0);
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
Convert a time system into a textual description. Composite overtime format is
used, or the word infinite.
*/
void time_system_to_str(
    char * dst,
    time_system * ts
){
    if(ts->main_time == 0 && ts->byo_yomi_time > 0 && ts->byo_yomi_stones == 0)
    {
        snprintf(dst, MAX_PAGE_SIZ, "infinite");
        return;
    }

    char * abs = alloc();
    char * byo = alloc();

    format_nr_millis(abs, ts->main_time);
    format_nr_millis(byo, ts->byo_yomi_time);

    snprintf(dst, MAX_PAGE_SIZ, "%s+%ux%s/%u", abs, ts->byo_yomi_periods, byo,
        ts->byo_yomi_stones);

    release(byo);
    release(abs);
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

static bool _process_main_time(
    time_system * dst,
    const char * src
){
    d32 val = str_to_milliseconds(src);
    if(val < 0)
        return false;

    dst->main_time = val;
    return true;
}

static bool _process_nr_periods(
    time_system * dst,
    const char * src
){
    d32 i;
    if(!parse_int(&i, src) || i <= 0)
        return false;

    dst->byo_yomi_periods = i;
    return true;
}

static bool _process_byo_yomi_time(
    time_system * dst,
    const char * src
){
    d32 val = str_to_milliseconds(src);
    if(val <= 0)
        return false;

    dst->byo_yomi_time = val;
    return true;
}

static bool _process_period_stones(
    time_system * dst,
    const char * src
){
    d32 i;
    if(!parse_int(&i, src) || i < 0)
        return false;

    dst->byo_yomi_stones = i;
    return true;
}

/*
Convert a string in the format time+numberxtime/number to a time system struct.
RETURNS true if successful and value stored in dst
*/
bool str_to_time_system(
    time_system * dst,
    const char * src
){
    dst->main_time = 0;
    dst->byo_yomi_stones = 1;
    dst->byo_yomi_time = 0;
    dst->byo_yomi_periods = 1;
    dst->can_timeout = true;

    if(strcmp(src, "infinite") == 0)
    {
        dst->byo_yomi_stones = 0;
        dst->byo_yomi_time = 1;
        dst->can_timeout = false;
        return true;
    }


    char * s = alloc();
    char * original_ptr = s;

    memcpy(s, src, strlen(src) + 1);
    s = trim(s);

    /*
    time + ...
    */
    char * char_idx = strchr(s, '+');
    if(char_idx != NULL)
        char_idx[0] = 0;

    if(!_process_main_time(dst, s))
    {
        release(original_ptr);
        return false;
    }

    if(char_idx == NULL)
    {
        release(original_ptr);
        return dst->main_time > 0;
    }

    s = char_idx + 1;
    if(!s[0])
    {
        release(original_ptr);
        return false;
    }

    /*
    ... + number x ...
    */
    char_idx = strchr(s, 'x');
    if(char_idx != NULL)
    {
        char_idx[0] = 0;
        if(!_process_nr_periods(dst, s))
        {
            release(original_ptr);
            return false;
        }
        s = char_idx + 1;
        if(!s[0])
        {
            release(original_ptr);
            return false;
        }
    }

    /*
    ... x time / ...
    */
    char_idx = strchr(s, '/');
    if(char_idx != NULL)
        char_idx[0] = 0;

    if(!_process_byo_yomi_time(dst, s))
    {
        release(original_ptr);
        return false;
    }

    /*
    ... / number
    */
    if(char_idx != NULL)
    {
        s = char_idx + 1;
        if(!_process_period_stones(dst, s))
        {
            release(original_ptr);
            return false;
        }
    }

    release(original_ptr);

    if(dst->byo_yomi_time == 0)
        return false;
    if(dst->byo_yomi_stones == 0){
        /* infinite time */
        dst->main_time = 0;
        dst->byo_yomi_periods = 1;
        dst->byo_yomi_stones = 0;
        dst->byo_yomi_time = 1;
        dst->can_timeout = false;
    }
    return true;
}
