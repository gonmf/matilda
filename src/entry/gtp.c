/*
Matilda application with GTP interface

Attempts to understand when to perform internal maintenance; see the help for
more information. Besides the standard GTP commands it also supports commands
that allow exporting SGF files, perform maintenance on-demand, etc. Run
list-commands or help for more information.

With a whole-game context it also performs time control adjustments, prevents
positional superkos, performs maintenance and thinking between turns, etc. This
under the assumption the program is playing as one of the players only; which is
enabled at startup.

GTP mode has GTP version 2 draft 2 support, online:
http://www.lysator.liu.se/~gunnar/gtp/gtp2-spec-draft2/gtp2-spec.html

For an explanation of the extra commands support read the documentation file
GTP_README.
*/

#include "matilda.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "alloc.h"
#include "analysis.h"
#include "board.h"
#include "engine.h"
#include "file_io.h"
#include "flog.h"
#include "game_record.h"
#include "opening_book.h"
#include "pts_file.h"
#include "randg.h"
#include "random_play.h"
#include "scoring.h"
#include "sgf.h"
#include "state_changes.h"
#include "stringm.h"
#include "time_ctrl.h"
#include "timem.h"
#include "transpositions.h"
#include "types.h"
#include "version.h"

extern d16 komi;
extern u32 network_roundtrip_delay;
extern bool network_round_trip_set;

const char * supported_commands[] =
{
    "boardsize",
    "clear_board",
    "clear_cache",
#if !defined(__MACH__)
    "cputime",
#endif
    "echo",
    "echo_err",
    "exit",
    "final_score",
    "final_status_list",
    "genmove",
    "gg-undo",
    "gomill-cpu_time",
    "gomill-describe_engine",
    "help",
    "kgs-game_over",
    "kgs-genmove_cleanup",
    "kgs-time_settings",
    "known_command",
    "komi",
    "list_commands",
    "loadsgf",
    "mtld-final_position",
    "mtld-last_evaluation",
    "mtld-ponder",
    "mtld-review_game",
    "name",
    "place_free_handicap",
    "play",
    "printsgf",
    "protocol_version",
    "quit",
    "reg_genmove",
    "set_free_handicap",
    "showboard",
    "time_left",
    "time_settings",
    "undo",
    "version",
    NULL
};

extern bool estimate_score;
extern bool time_system_overriden;
extern bool save_all_games_to_file;
extern bool resign_on_timeout;
extern game_record current_game;
extern time_system current_clock_black;
extern time_system current_clock_white;

static bool out_on_time_warning = false;

/*
These two fields are used to try to guess which player is the program. This is
only used for naming the players in SGF records.
*/
static bool has_genmoved_as_black = false;
static bool has_genmoved_as_white = false;

static u64 request_received_mark;

static out_board last_out_board;


static void update_player_names()
{
    if(has_genmoved_as_black == has_genmoved_as_white)
    {
        snprintf(current_game.black_name, MAX_PLAYER_NAME_SIZ, "black");
        snprintf(current_game.white_name, MAX_PLAYER_NAME_SIZ, "white");
        return;
    }

    if(has_genmoved_as_black)
    {
        snprintf(current_game.black_name, MAX_PLAYER_NAME_SIZ, "matilda");
        snprintf(current_game.white_name, MAX_PLAYER_NAME_SIZ, "white");
        return;
    }

    snprintf(current_game.black_name, MAX_PLAYER_NAME_SIZ, "black");
    snprintf(current_game.white_name, MAX_PLAYER_NAME_SIZ, "matilda");
    return;
}

static void error_msg(
    FILE * fp,
    int id,
    const char * s
){
    char * buf = alloc();
    if(id == -1)
        snprintf(buf, MAX_PAGE_SIZ, "? %s\n\n", s);
    else
        snprintf(buf, MAX_PAGE_SIZ, "?%d %s\n\n", id, s);

    size_t w = fwrite(buf, 1, strlen(buf), fp);
    if(w != strlen(buf))
        flog_crit("gtp", "failed to write to comm. file descriptor");

    fflush(fp);

    flog_prot("gtp", buf);
    release(buf);
}

static void answer_msg(
    FILE * fp,
    int id,
    const char * s
){
    char * buf = alloc();
    if(s == NULL || strlen(s) == 0)
    {
        if(id == -1)
            snprintf(buf, MAX_PAGE_SIZ, "= \n\n");
        else
            snprintf(buf, MAX_PAGE_SIZ, "=%d\n\n", id);
    }
    else
    {
        if(id == -1)
            snprintf(buf, MAX_PAGE_SIZ, "= %s\n\n", s);
        else
            snprintf(buf, MAX_PAGE_SIZ, "=%d %s\n\n", id, s);
    }

    size_t w = fwrite(buf, 1, strlen(buf), fp);
    if(w != strlen(buf))
        flog_crit("gtp", "failed to write to comm. file descriptor");

    fflush(fp);

    flog_prot("gtp", buf);
    release(buf);
}

static void gtp_protocol_version(
    FILE * fp,
    int id
){
    answer_msg(fp, id, "2");
}

static void gtp_name(
    FILE * fp,
    int id
){
    answer_msg(fp, id, "matilda");
}

static void gtp_version(
    FILE * fp,
    int id
){
    char * s = alloc();
    snprintf(s, MAX_PAGE_SIZ, "%s", MATILDA_VERSION);
    answer_msg(fp, id, s);
    release(s);
}

static void gtp_known_command(
    FILE * fp,
    int id,
    const char * command_name
){
    u16 i = 0;
    while(supported_commands[i] != NULL)
    {
        if(strcmp(supported_commands[i], command_name) == 0)
        {
            answer_msg(fp, id, "true");
            return;
        }
        ++i;
    }
    answer_msg(fp, id, "false");
}

static void gtp_list_commands(
    FILE * fp,
    int id
){
    char * buf = alloc();
    u16 idx = 0;

    for(u16 i = 0; supported_commands[i] != NULL; ++i)
        idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "%s\n",
            supported_commands[i]);

    if(idx > 0)
        buf[idx - 1] = 0;

    answer_msg(fp, id, buf);
    release(buf);
}

/*
Non-standard addition to the protocol: it asks the engine to ponder on the
current game state. Receives time to think in seconds.
RETURNS text description
*/
static void gtp_ponder(
    FILE * fp,
    int id,
    const char * timestr /* in seconds */
){
    d32 seconds;
    if(!parse_int(timestr, &seconds) || seconds < 1)
    {
        error_msg(fp, id, "syntax error");
        return;
    }

    board current_state;
    current_game_state(&current_state, &current_game);
    bool is_black = current_player_color(&current_game);

    char * buf = alloc();

    request_opinion(buf, &current_state, is_black, seconds * 1000);

    answer_msg(fp, id, buf);
    release(buf);
}

/*
Review all previous plays in the selected time in seconds per turn. Receives
time to think in seconds.
RETURNS text description
*/
static void gtp_review_game(
    FILE * fp,
    int id,
    const char * timestr /* in seconds */
){
    d32 seconds;
    if(!parse_int(timestr, &seconds) || seconds < 1)
    {
        error_msg(fp, id, "syntax error");
        return;
    }

    new_match_maintenance();

    char * buf = alloc();
    u32 idx = 0;

    out_board out_b;
    board b;
    first_game_state(&b, &current_game);
    bool is_black = first_player_color(&current_game);

    for(u16 t = 0; t < current_game.turns; ++t)
    {
        u64 curr_time = current_time_in_millis();
        u64 stop_time = curr_time + seconds * 1000;
        u64 early_stop_time = curr_time + seconds * 500;
        evaluate_position(&b, is_black, &out_b, stop_time, early_stop_time);

        move best = select_play_fast(&out_b);
        move actual = current_game.moves[t];
        char * s = alloc();
        if(is_board_move(actual))
        {
            coord_to_alpha_num(s, actual);
            idx += snprintf(buf + idx, 4 * 1024 - idx,
                "%u: (%c) Actual: %s (%.3f)", t, is_black ? 'B' : 'W', s,
                out_b.value[actual]);
        }
        else
            idx += snprintf(buf + idx, 4 * 1024 - idx,
                "%u: (%c) Actual: pass", t, is_black ? 'B' : 'W');

        if(is_board_move(best))
        {
            coord_to_alpha_num(s, best);
            idx += snprintf(buf + idx, 4 * 1024 - idx, " Best: %s (%.3f)\n", s,
                out_b.value[best]);
        }
        else
            idx += snprintf(buf + idx, 4 * 1024 - idx, " Best: pass\n");
        release(s);
        opt_turn_maintenance(&b, is_black);
        just_play_slow(&b, actual, is_black);
        is_black = !is_black;
    }

    answer_msg(fp, id, buf);
    release(buf);
}

static void gtp_quit(
    FILE * fp,
    int id
){
    answer_msg(fp, id, NULL);
    exit(EXIT_SUCCESS);
}

static void gtp_clear_cache(
    FILE * fp,
    int id
){
    new_match_maintenance();
    answer_msg(fp, id, NULL);
}

static void gtp_clear_board(
    FILE * fp,
    int id
){
    answer_msg(fp, id, NULL);

    if(save_all_games_to_file && current_game.turns > 0)
    {
        update_player_names();
        char * filename = alloc();
        if(export_game_as_sgf_auto_named(&current_game, filename))
        {
            char * buf = alloc();
            snprintf(buf, MAX_PAGE_SIZ, "game record exported to %s", filename);
            flog_info("gtp", buf);
            release(buf);
        }
        else
            flog_warn("gtp", "failed to export game record to file");
        release(filename);
    }

    has_genmoved_as_black = false;
    has_genmoved_as_white = false;
    if(current_game.turns > 0)
        new_match_maintenance();
    clear_game_record(&current_game);
    reset_clock(&current_clock_black);
    reset_clock(&current_clock_white);
    out_on_time_warning = false;
}

static void gtp_boardsize(
    FILE * fp,
    int id,
    const char * new_size
){
    d32 ns;
    if(!parse_int(new_size, &ns))
    {
        error_msg(fp, id, "syntax error");
        return;
    }

    if(ns != BOARD_SIZ)
    {
        error_msg(fp, id, "unacceptable size");
        flog_warn("gtp", "changing the board size requires the program to be re\
compiled");
    }
    else
        answer_msg(fp, id, NULL);
}

static void gtp_komi(
    FILE * fp,
    int id,
    const char * new_komi
){
    double komid;
    if(!parse_float(new_komi, &komid))
    {
        error_msg(fp, id, "syntax error");
        return;
    }
    answer_msg(fp, id, NULL);

    char * kstr = alloc();
    komi_to_string(kstr, komi);

    d16 komi2 = (d16)(komid * 2.0);
    if(komi != komi2)
    {
        char * kstr2 = alloc();
        komi_to_string(kstr2, komi2);

        fprintf(stderr, "komidashi changed from %s to %s stones\n", kstr,
            kstr2);

        release(kstr2);
        komi = komi2;
    }
    else
        fprintf(stderr, "komidashi kept at %s stones\n", kstr);

    release(kstr);
}

static void gtp_play(
    FILE * fp,
    int id,
    const char * color,
    char * vertex,
    bool allow_skip
){
    bool is_black;
    if(!parse_color(color, &is_black))
    {
        error_msg(fp, id, "syntax error");
        return;
    }

    if(allow_skip)
    {
        lower_case(vertex);
        if(strcmp(vertex, "skip") == 0)
        {
            add_play_out_of_order(&current_game, is_black, NONE);
            current_game.game_finished = false;
            board current_state;
            current_game_state(&current_state, &current_game);
            opt_turn_maintenance(&current_state, !is_black);
            answer_msg(fp, id, NULL);
            return;
        }
    }

    move m;
    if(!parse_gtp_vertex(vertex, &m))
    {
        error_msg(fp, id, "syntax error");
        return;
    }

    if(m == NONE)
    {
        /* Resign */
        current_game.game_finished = true;
        current_game.resignation = true;
        current_game.final_score = is_black ? -1 : 1;
        answer_msg(fp, id, NULL);
        return;
    }

    if(!play_is_legal(&current_game, m, is_black))
    {
        error_msg(fp, id, "illegal move");
        return;
    }
    answer_msg(fp, id, NULL);

    add_play_out_of_order(&current_game, is_black, m);
    current_game.game_finished = false;
    board current_state;
    current_game_state(&current_state, &current_game);
    opt_turn_maintenance(&current_state, !is_black);
}

/*
Generic genmove functions that fulfills the needs of the GTP.
*/
static void generic_genmove(
    FILE * fp,
    int id,
    const char * color,
    bool reg
){
    bool is_black;
    if(!parse_color(color, &is_black))
    {
        error_msg(fp, id, "syntax error");
        return;
    }

    char * buf = alloc();
    out_board out_b;

    if(is_black)
        has_genmoved_as_black = true;
    else
        has_genmoved_as_white = true;

    board current_state;
    current_game_state(&current_state, &current_game);

    /*
    We may be asked to play with the same color two times in a row.
    This may trigger false ko violations; so we prevent them here.
    */
    if(current_game.turns > 0 && current_player_color(&current_game) !=
        is_black)
    {
        current_state.last_played = NONE;
        current_state.last_eaten = NONE;
    }

    u32 time_to_play = 0;

#if !LIMIT_BY_PLAYOUTS
    time_system * curr_clock = is_black ? &current_clock_black :
        &current_clock_white;

    u16 stones = stone_count(current_state.p);
    time_to_play = calc_time_to_play(curr_clock, stones);

    snprintf(buf, MAX_PAGE_SIZ, "time to play: %u.%03us\n", time_to_play /
        1000, time_to_play % 1000);
    flog_info("gtp", buf);
#endif

    u64 stop_time = request_received_mark + time_to_play;
    u64 early_stop_time = request_received_mark + (time_to_play / 2);
    bool has_play = evaluate_position(&current_state, is_black, &out_b,
        stop_time, early_stop_time);

    memcpy(&last_out_board, &out_b, sizeof(out_board));

    /*
    The game is lost, resign or play something at random.
    */
    if(!has_play)
    {
#if CAN_RESIGN
        answer_msg(fp, id, "resign");

        snprintf(buf, MAX_PAGE_SIZ, "matilda playing as %s (%c) resigns\n",
            is_black ? "black" : "white", is_black ? BLACK_STONE_CHAR :
            WHITE_STONE_CHAR);
        flog_warn("gtp", buf);

        current_game.game_finished = true;
        current_game.resignation = true;
        current_game.final_score = is_black ? -1 : 1;

        release(buf);
        return;
#endif
        /* Pass */
        clear_out_board(&out_b);
    }

    move m = select_play(&out_b, is_black, &current_game);

    if(!reg)
    {
        add_play_out_of_order(&current_game, is_black, m);
        current_game.game_finished = false;

#if !LIMIT_BY_PLAYOUTS
        u32 elapsed = (u32)(current_time_in_millis() -
            request_received_mark);

        advance_clock(curr_clock, elapsed);
        if(curr_clock->timed_out)
        {
            if(resign_on_timeout)
            {
                answer_msg(fp, id, "resign");

                snprintf(buf, MAX_PAGE_SIZ, "matilda playing as %s (%c) res\
igns because of timeout\n", is_black ? "black" : "white", is_black ?
                    BLACK_STONE_CHAR : WHITE_STONE_CHAR);
                flog_warn("gtp", buf);

                current_game.game_finished = true;
                current_game.resignation = true;
                current_game.final_score = is_black ? -1 : 1;




#if 0

                /* TODO just for counting resigns on timeout for paper */
                flog_dbug("gtp", "TIMEOUT\n");


#endif




                release(buf);
                return;
            }
            if(!out_on_time_warning)
            {
                out_on_time_warning = true;
                snprintf(buf, MAX_PAGE_SIZ, "matilda is believed to have lo\
st on time");
                flog_warn("gtp", buf);
            }
            /* we don't do anything else when timed out */
        }
#endif
    }

    coord_to_gtp_vertex(buf, m);
    answer_msg(fp, id, buf);
    release(buf);
}

static void gtp_genmove(
    FILE * fp,
    int id,
    const char * color
){
    generic_genmove(fp, id, color, false);
}

static void gtp_reg_genmove(
    FILE * fp,
    int id,
    const char * color
){
    generic_genmove(fp, id, color, true);
}

static void gtp_echo(
    FILE * fp,
    int id,
    u16 argc,
    char * argv[],
    bool print_to_stderr
){
    char * buf = alloc();
    d32 idx = 0;
    if(argc > 0)
        idx = snprintf(buf, MAX_PAGE_SIZ, "%s", argv[0]);

    for(u16 k = 1; k < argc; ++k)
        idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, " %s", argv[k]);
    answer_msg(fp, id, buf);

    if(print_to_stderr)
        fprintf(stderr, "%s\n", buf);

    release(buf);
}

static void gtp_time_settings(
    FILE * fp,
    int id,
    const char * main_time,
    const char * byo_yomi_time,
    const char * byo_yomi_stones
){
    if(LIMIT_BY_PLAYOUTS)
    {
        flog_warn("gtp", "attempted to set time settings when matilda was compi\
led to use a constant number of simulations per turn in MCTS; request ignored");
        answer_msg(fp, id, NULL);
        return;
    }

    if(time_system_overriden)
    {
        answer_msg(fp, id, NULL);
        return;
    }

    char * previous_ts_as_s = alloc();
    time_system_to_str(previous_ts_as_s, &current_clock_black);

    d32 new_main_time;
    d32 new_byo_yomi_time;
    d32 new_byo_yomi_stones;
    if(!parse_int(main_time, &new_main_time) || new_main_time < 0 ||
        new_main_time >= 2147484)
    {
        error_msg(fp, id, "syntax error");
        release(previous_ts_as_s);
        return;
    }
    if(!parse_int(byo_yomi_time, &new_byo_yomi_time) || new_byo_yomi_time < 0 ||
        new_byo_yomi_time >= 2147484)
    {
        error_msg(fp, id, "syntax error");
        release(previous_ts_as_s);
        return;
    }
    if(!parse_int(byo_yomi_stones, &new_byo_yomi_stones) || new_byo_yomi_stones
        < 0)
    {
        error_msg(fp, id, "syntax error");
        release(previous_ts_as_s);
        return;
    }

    answer_msg(fp, id, NULL);

    if(new_main_time == 0 && new_byo_yomi_time > 0 && new_byo_yomi_stones == 0)
    {
        /* no time limit */
        set_time_per_turn(&current_clock_black, DEFAULT_TIME_PER_TURN);
        set_time_per_turn(&current_clock_white, DEFAULT_TIME_PER_TURN);
        current_clock_black.can_timeout = false;
        current_clock_white.can_timeout = false;
    }
    else
    {
        set_time_system(&current_clock_black, new_main_time * 1000,
            new_byo_yomi_time * 1000, new_byo_yomi_stones, 1);
        set_time_system(&current_clock_white, new_main_time * 1000,
            new_byo_yomi_time * 1000, new_byo_yomi_stones, 1);
    }

    char * new_ts_as_s = alloc();
    time_system_to_str(new_ts_as_s, &current_clock_black);

    char * buf = alloc();
    if(strcmp(previous_ts_as_s, new_ts_as_s) == 0)
        snprintf(buf, MAX_PAGE_SIZ, "clock settings kept at %s for both p\
layers", previous_ts_as_s);
    else
        snprintf(buf, MAX_PAGE_SIZ, "clock settings changed from %s to %s\
 for both players", previous_ts_as_s, new_ts_as_s);

    flog_info("gtp", buf);

    release(buf);
    release(new_ts_as_s);
    release(previous_ts_as_s);
}

static void gtp_kgs_time_settings(
    FILE * fp,
    int id,
    const char * systemstr,
    const char * main_time,
    const char * byo_yomi_time,
    const char * byo_yomi_stones
){
    if(LIMIT_BY_PLAYOUTS)
    {
        flog_warn("gtp", "attempted to set time settings when matilda was compi\
led to use a constant number of simulations per turn in MCTS; request ignored");
        answer_msg(fp, id, NULL);
        return;
    }

    if(time_system_overriden)
    {
        answer_msg(fp, id, NULL);
        return;
    }

    if(systemstr == NULL)
    {
        error_msg(fp, id, "syntax error");
        return;
    }

    char * previous_ts_as_s = alloc();
    time_system_to_str(previous_ts_as_s, &current_clock_black);

    if(strcmp(systemstr, "none") == 0)
    {
        set_time_per_turn(&current_clock_black, DEFAULT_TIME_PER_TURN);
        set_time_per_turn(&current_clock_white, DEFAULT_TIME_PER_TURN);
    }
    else
        if(strcmp(systemstr, "absolute") == 0)
        {
            d32 new_main_time;
            if(main_time == NULL || !parse_int(main_time, &new_main_time) ||
                new_main_time < 0 || new_main_time >= 2147484)
            {
                error_msg(fp, id, "syntax error");
                release(previous_ts_as_s);
                return;
            }
            set_time_system(&current_clock_black, new_main_time * 1000, 0, 0,
                0);
            set_time_system(&current_clock_white, new_main_time * 1000, 0, 0,
                0);
        }
        else
            if(strcmp(systemstr, "byoyomi") == 0)
            {
                const char * byo_yomi_periods = byo_yomi_stones;

                d32 new_main_time;
                d32 new_byo_yomi_time;
                d32 new_byo_yomi_periods;
                if(main_time == NULL || !parse_int(main_time, &new_main_time) ||
                    new_main_time < 0 || new_main_time >= 2147484)
                {
                    error_msg(fp, id, "syntax error");
                    release(previous_ts_as_s);
                    return;
                }
                if(byo_yomi_time == NULL || !parse_int(byo_yomi_time,
                    &new_byo_yomi_time) || new_byo_yomi_time < 0 ||
                    new_byo_yomi_time >= 2147484)
                {
                    error_msg(fp, id, "syntax error");
                    release(previous_ts_as_s);
                    return;
                }
                if(byo_yomi_periods == NULL || !parse_int(byo_yomi_periods,
                    &new_byo_yomi_periods) || new_byo_yomi_periods < 0)
                {
                    error_msg(fp, id, "syntax error");
                    release(previous_ts_as_s);
                    return;
                }

                set_time_system(&current_clock_black, new_main_time * 1000,
                    new_byo_yomi_time * 1000, 1, new_byo_yomi_periods);
                set_time_system(&current_clock_white, new_main_time * 1000,
                    new_byo_yomi_time * 1000, 1, new_byo_yomi_periods);
            }else
                if(strcmp(systemstr, "canadian") == 0)
                {
                    d32 new_main_time;
                    d32 new_byo_yomi_time;
                    d32 new_byo_yomi_stones;
                    if(main_time == NULL || !parse_int(main_time,
                        &new_main_time) || new_main_time < 0 || new_main_time >=
                        2147484)
                    {
                        error_msg(fp, id, "syntax error");
                        release(previous_ts_as_s);
                        return;
                    }
                    if(byo_yomi_time == NULL || !parse_int(byo_yomi_time,
                        &new_byo_yomi_time) || new_byo_yomi_time < 0 ||
                        new_byo_yomi_time >= 2147484)
                    {
                        error_msg(fp, id, "syntax error");
                        release(previous_ts_as_s);
                        return;
                    }
                    if(byo_yomi_stones == NULL || !parse_int(byo_yomi_stones,
                        &new_byo_yomi_stones) || new_byo_yomi_stones < 0)
                    {
                        error_msg(fp, id, "syntax error");
                        release(previous_ts_as_s);
                        return;
                    }

                    set_time_system(&current_clock_black, new_main_time * 1000,
                        new_byo_yomi_time * 1000, new_byo_yomi_stones, 1);
                    set_time_system(&current_clock_white, new_main_time * 1000,
                        new_byo_yomi_time * 1000, new_byo_yomi_stones, 1);
                }
                else
                {
                    error_msg(fp, id, "syntax error");
                    release(previous_ts_as_s);
                    return;
                }

    answer_msg(fp, id, NULL);

    char * new_ts_as_s = alloc();
    time_system_to_str(new_ts_as_s, &current_clock_black);

    char * buf = alloc();
    if(strcmp(previous_ts_as_s, new_ts_as_s) == 0)
        snprintf(buf, MAX_PAGE_SIZ,
            "clock settings kept at %s for both players", previous_ts_as_s);
    else
        snprintf(buf, MAX_PAGE_SIZ, "clock settings changed from %s to %s for b\
oth players", previous_ts_as_s, new_ts_as_s);

    flog_info("gtp", buf);
    release(buf);

    release(new_ts_as_s);
    release(previous_ts_as_s);
}

static void gtp_time_left(
    FILE * fp,
    int id,
    const char * color,
    const char * _time,
    const char * stones
){
    if(LIMIT_BY_PLAYOUTS)
    {
        flog_warn("gtp", "attempted to set time settings when matilda was compi\
led to use a constant number of simulations per turn in MCTS; request ignored");
        answer_msg(fp, id, NULL);
        return;
    }

    if(time_system_overriden)
    {
        answer_msg(fp, id, NULL);
        return;
    }

    bool is_black;
    if(!parse_color(color, &is_black))
    {
        error_msg(fp, id, "syntax error");
        return;
    }
    d32 new_time_remaining;
    d32 new_byo_yomi_stones_remaining;
    if(!parse_int(_time, &new_time_remaining) || new_time_remaining < 0)
    {
        error_msg(fp, id, "syntax error");
        return;
    }
    if(!parse_int(stones, &new_byo_yomi_stones_remaining) ||
        new_byo_yomi_stones_remaining < 0)
    {
        error_msg(fp, id, "syntax error");
        return;
    }

    answer_msg(fp, id, NULL);


    time_system * curr_clock = is_black ? &current_clock_black :
        &current_clock_white;

    if(new_byo_yomi_stones_remaining == 0)
    {
        /* Main time is still counting down */
        curr_clock->main_time_remaining = new_time_remaining * 1000;
    }
    else
    {
        /* Byo-yomi time */
        curr_clock->byo_yomi_time_remaining = new_time_remaining * 1000;
        curr_clock->byo_yomi_stones_remaining = new_byo_yomi_stones_remaining;
    }
}

static void gtp_cputime(
    FILE * fp,
    int id
){
#if defined(__MACH__)
    error_msg(fp, id, "command unsupported");
    flog_warn("gtp", "cputime requested in OSX (command unsupported)");
#else
    clockid_t clockid;
    struct timespec ts;

    pid_t pid = getpid();

    if(clock_getcpuclockid(pid, &clockid) != 0)
    {
        error_msg(fp, id, "operation failed");
        return;
    }

    if(clock_gettime(clockid, &ts) == -1)
    {
        error_msg(fp, id, "operation failed");
        return;
    }

    char * buf = alloc();
    snprintf(buf, MAX_PAGE_SIZ, "%u.%03lu", (u32)ts.tv_sec, ((u64)ts.tv_nsec) /
        1000000);
    answer_msg(fp, id, buf);
    release(buf);
#endif
}

static void gtp_final_status_list(
    FILE * fp,
    int id,
    const char * status
){
    char * buf = alloc();
    char * mstr = alloc();
    d32 idx = 0;

    bool is_black = current_player_color(&current_game);
    board current_state;
    current_game_state(&current_state, &current_game);

    u8 e[TOTAL_BOARD_SIZ];
    estimate_final_position(e, &current_state, is_black);

    if(strcmp(status, "dead") == 0)
    {
        for(move m = 0; m < TOTAL_BOARD_SIZ; ++m)
            if(current_state.p[m] != EMPTY && e[m] != current_state.p[m])
            {
                coord_to_alpha_num(mstr, m);
                idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "%s\n", mstr);
            }
        answer_msg(fp, id, buf);
    }
    else
        if(strcmp(status, "alive") == 0)
        {
            for(move m = 0; m < TOTAL_BOARD_SIZ; ++m)
                if(current_state.p[m] != EMPTY && e[m] == current_state.p[m])
                {
                    coord_to_alpha_num(mstr, m);
                    idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "%s\n",
                        mstr);
                }
            answer_msg(fp, id, buf);
        }
        else
        {
            if(strcmp(status, "seki") == 0)
            {
                error_msg(fp, id, "seki detection unsupported");
                flog_warn("gtp", "final_status_list with seki parameter unsuppo\
rted");
            }
            else
                error_msg(fp, id, "syntax error");
        }

    release(mstr);
    release(buf);
}

static void gtp_gomill_describe_engine(
    FILE * fp,
    int id
){
    char * s = alloc();
    build_info(s);
    answer_msg(fp, id, s);
    release(s);
}

static void gtp_showboard(
    FILE * fp,
    int id
){
    board b;
    current_game_state(&b, &current_game);
    char * str = alloc();
    char * str2 = alloc();
    board_to_string(str2, b.p, b.last_played, b.last_eaten);

    snprintf(str, MAX_PAGE_SIZ, "\n%s", str2);
    answer_msg(fp, id, str);

    release(str2);
    release(str);
}

/*
RETURNS false on failure
*/
static bool generic_undo(
    u16 moves
){
    if(moves == 0 || current_game.turns < moves)
        return false;

    for(u16 k = 0; k < moves; ++k)
        if(!undo_last_play(&current_game))
            return false;

     /* after undoing we will be at game start */
    if(current_game.turns == 0)
        new_match_maintenance();

    return true;
}

static void gtp_undo(
    FILE * fp,
    int id
){
    if(generic_undo(1))
        answer_msg(fp, id, NULL);
    else
        error_msg(fp, id, "cannot undo");
}

static void gtp_undo_multiple(
    FILE * fp,
    int id,
    const char * number
){
    d32 moves;
    if(number == NULL)
        moves = 1;
    else
        if(!parse_int(number, &moves) || moves < 1)
        {
            error_msg(fp, id, "syntax error");
            return;
        }

    if(generic_undo(moves))
        answer_msg(fp, id, NULL);
    else
        error_msg(fp, id, "cannot undo");
}

static void gtp_last_evaluation(
    FILE * fp,
    int id
){
    char * s = alloc();
    out_board_to_string(s, &last_out_board);
    answer_msg(fp, id, s);
    release(s);
}

static void gtp_final_position(
    FILE * fp,
    int id
){
    board current_state;
    current_game_state(&current_state, &current_game);
    bool is_black = current_player_color(&current_game);

    u8 e[TOTAL_BOARD_SIZ];
    estimate_final_position(e, &current_state, is_black);

    char * s = alloc();
    board_to_string(s, e, NONE, NONE);
    answer_msg(fp, id, s);
    release(s);
}

static void gtp_final_score(
    FILE * fp,
    int id
){
    d16 score;
    if(estimate_score)
    {
        board current_state;
        current_game_state(&current_state, &current_game);
        bool is_black = current_player_color(&current_game);
        score = score_estimate(&current_state, is_black);
    }else
        score = 0;

    current_game.game_finished = true;
    current_game.final_score = score;

    char * s = alloc();
    score_to_string(s, score);
    answer_msg(fp, id, s);
    release(s);
}

static void gtp_place_free_handicap(
    FILE * fp,
    int id,
    const char * nstones
){
    d32 num_stones;
    if(!parse_int(nstones, &num_stones) || num_stones < 1)
    {
        error_msg(fp, id, "syntax error");
        return;
    }
    board current_state;
    current_game_state(&current_state, &current_game);
    if(stone_count(current_state.p) > 0)
    {
        error_msg(fp, id, "board is not empty");
        return;
    }
    if(num_stones < 2 || num_stones > TOTAL_BOARD_SIZ - 1)
    {
        error_msg(fp, id, "invalid number of stones");
        return;
    }

    char * buf = alloc();
    u32 idx = 0;
    char * mstr = alloc();

    move_seq handicaps;
    get_ordered_handicap(&handicaps);
    for(move i = 0; i < handicaps.count && num_stones > 0; ++i)
    {
        move m = handicaps.coord[i];
        if(!add_handicap_stone(&current_game, m))
            flog_crit("gtp", "add handicap stone failed (1)");

        --num_stones;
        coord_to_alpha_num(mstr, m);
        idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "%s ", mstr);
    }

    /*
    If the user wants even more handicap stones, choose randomly
    */
    while(num_stones > 0)
    {
        current_game_state(&current_state, &current_game);
        move m = random_play2(&current_state, true);

        if(!add_handicap_stone(&current_game, m))
            flog_crit("gtp", "add handicap stone failed (2)");

        --num_stones;
        coord_to_alpha_num(mstr, m);
        idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "%s ", mstr);
    }

    answer_msg(fp, id, buf);

    release(mstr);
    release(buf);
}

static void gtp_set_free_handicap(
    FILE * fp,
    int id,
    u16 num_vertices,
    char * vertices[]
){
    if(current_game.turns > 0)
    {
        error_msg(fp, id, "board is not empty");
        return;
    }
    if(num_vertices < 2 || num_vertices > TOTAL_BOARD_SIZ - 1)
    {
        error_msg(fp, id, "bad vertex list");
        return;
    }
    for(u16 v = 0; v < num_vertices; ++v)
    {
        move m;
        if(!parse_gtp_vertex(vertices[v], &m) || m == PASS)
        {
            error_msg(fp, id, "bad vertex list");
            return;
        }
        if(!add_handicap_stone(&current_game, m))
        {
            error_msg(fp, id, "bad vertex list");
            return;
        }
    }

    answer_msg(fp, id, NULL);
}

static void gtp_loadsgf(
    FILE * fp,
    int id,
    const char * filename,
    const char * move_number /* optional */
){
    if(!validate_filename(filename))
    {
        fprintf(stderr, "illegal file name\n");
        error_msg(fp, id, "cannot load file");
        return;
    }

    d32 move_until;
    if(move_number == NULL)
        move_until = MAX_GAME_LENGTH;
    else
        if(!parse_int(move_number, &move_until) || move_until < 1)
        {
            error_msg(fp, id, "syntax error");
            return;
        }

    char * buf = alloc();
    snprintf(buf, MAX_PAGE_SIZ, "%s%s", get_data_folder(), filename);

    game_record tmp;

    bool imported = import_game_from_sgf(&tmp, buf);
    if(!imported)
    {
        error_msg(fp, id, "cannot load file");
        release(buf);
        return;
    }

    answer_msg(fp, id, NULL);

    tmp.turns = MIN(tmp.turns, move_until - 1);

    memcpy(&current_game, &tmp, sizeof(game_record));

    release(buf);
}

static void gtp_printsgf(
    FILE * fp,
    int id,
    const char * filename
){
    update_player_names();

    char * buf = alloc();

    if(filename == NULL)
    {
        export_game_as_sgf_to_buffer(&current_game, buf, MAX_PAGE_SIZ);
        answer_msg(fp, id, buf);
    }
    else
    {
        if(!validate_filename(filename))
        {
            error_msg(fp, id, "cannot save file");
            fprintf(stderr, "illegal file name\n");
            release(buf);
            return;
        }

        snprintf(buf, MAX_PAGE_SIZ, "%s%s", get_data_folder(), filename);

        bool success = export_game_as_sgf(&current_game, buf);
        if(!success)
        {
            error_msg(fp, id, "cannot create file");
            fprintf(stderr, "could not create file %s\n", buf);
        }
        else
        {
            answer_msg(fp, id, NULL);
            fprintf(stderr, "saved to file %s\n", buf);
        }
    }

    release(buf);
}

/*
Main function for GTP mode - performs command selction.

Thinking in opponents turns should be disabled for most matches. It doesn't
limit itself, so it will keep using the MCTS if used previously until the
opponent plays or memory runs out.
*/
void main_gtp(
    bool think_in_opt_turn
){
    load_hoshi_points();
    transpositions_table_init();

    flog_info("gtp", "matilda now running over GTP");
    char * s = alloc();
    build_info(s);
    flog_info("gtp", s);
    release(s);

    FILE * out_fp;
    int _out_fp = dup(STDOUT_FILENO);
    if(_out_fp == -1)
        flog_crit("gtp", "file descriptor duplication failure (1)");

    close(STDOUT_FILENO);
    out_fp = fdopen(_out_fp, "w");
    if(out_fp == NULL)
        flog_crit("gtp", "file descriptor duplication failure (2)");

    clear_out_board(&last_out_board);
    clear_game_record(&current_game);

#if DETECT_NETWORK_LATENCY
    u64 last_time_frame = 0;
    bool time_frame_set = false;
#endif

    fd_set readfs;
    memset(&readfs, 0, sizeof(fd_set));
    char * in_buf = alloc();

    while(1)
    {
        bool is_black = current_player_color(&current_game);

        board current_state;
        current_game_state(&current_state, &current_game);

        while(1)
        {
            if(think_in_opt_turn)
            {
                FD_ZERO(&readfs);
                FD_SET(STDIN_FILENO, &readfs);
                struct timeval tm;
                tm.tv_sec = 0;
                tm.tv_usec = 2000;

                int ready = select(STDIN_FILENO + 1, &readfs, NULL, NULL, &tm);
                if(ready == 0)
                {
                    evaluate_in_background(&current_state, is_black);
                    continue;
                }
            }
            break;
        }

        opt_turn_maintenance(&current_state, is_black);
        reset_mcts_can_resume();

        char * line = fgets(in_buf, MAX_PAGE_SIZ, stdin);
        request_received_mark = current_time_in_millis();

#if DETECT_NETWORK_LATENCY
        /*
        Network latency estimation
        */
        if(time_frame_set == false)
        {
            time_frame_set = true;
            last_time_frame = current_time_in_millis();
        }
        else
        {
            u64 tmp = current_time_in_millis();
            u32 roundtrip = tmp - last_time_frame;
            last_time_frame = tmp;
            if(network_round_trip_set == false)
            {
                network_roundtrip_delay = roundtrip;
                network_round_trip_set = true;
            }
            else
                if(roundtrip < network_roundtrip_delay){
                    network_roundtrip_delay = roundtrip;
                    char tabuf[64];
                    snprintf(tabuf, 64, "network latency compensation adjusted \
to %u milliseconds", network_roundtrip_delay);
                    flog_info("gtp", tabuf);
                }
        }
#endif

        if(line == NULL)
            break;

        line = strtok(line, "#");
        if(line == NULL)
            continue;

        line = trim(line);
        if(line == NULL)
            continue;

        flog_prot("gtp", line);

        char * save_ptr;
        char * id = strtok_r(line, " |", &save_ptr);
        d32 idn;
        char * cmd;
        if(parse_int(id, &idn))
            cmd = strtok_r(NULL, " |", &save_ptr);
        else
        {
            cmd = id;
            id = NULL;
            idn = -1;
        }

        if(cmd == NULL)
            continue;

        u16 argc = 0;
        char * args[TOTAL_BOARD_SIZ];
        for(u16 i = 0; i < TOTAL_BOARD_SIZ; ++i)
        {
            args[i] = strtok_r(NULL, " |", &save_ptr);
            if(args[i] == NULL)
            {
                ++i;
                for(; i < TOTAL_BOARD_SIZ; ++i)
                    args[i] = NULL;
                break;
            }
            ++argc;
        }

cmd_matcher:

        if(argc == 2 && strcmp(cmd, "play") == 0)
        {
            gtp_play(out_fp, idn, args[0], args[1], false);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "genmove") == 0)
        {
            gtp_genmove(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "reg_genmove") == 0)
        {
            gtp_reg_genmove(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "protocol_version") == 0)
        {
            gtp_protocol_version(out_fp, idn);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "name") == 0)
        {
            gtp_name(out_fp, idn);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "version") == 0)
        {
            gtp_version(out_fp, idn);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "known_command") == 0)
        {
            gtp_known_command(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 0 && (strcmp(cmd, "list_commands") == 0 || strcmp(cmd,
            "help") == 0))
        {
            gtp_list_commands(out_fp, idn);
            continue;
        }

        if(argc == 0 && (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0))
        {
            gtp_quit(out_fp, idn);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "boardsize") == 0)
        {
            gtp_boardsize(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "clear_board") == 0)
        {
            gtp_clear_board(out_fp, idn);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "kgs-game_over") == 0)
        {
            gtp_clear_board(out_fp, idn);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "komi") == 0)
        {
            gtp_komi(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "kgs-genmove_cleanup") == 0)
        {
            gtp_genmove(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "final_status_list") == 0)
        {
            gtp_final_status_list(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "showboard") == 0)
        {
            gtp_showboard(out_fp, idn);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "undo") == 0)
        {
            gtp_undo(out_fp, idn);
            continue;
        }

        if(argc <= 1 && strcmp(cmd, "gg-undo") == 0)
        {
            gtp_undo_multiple(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "mtld-ponder") == 0)
        {
            gtp_ponder(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "mtld-review_game") == 0)
        {
            gtp_review_game(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "final_score") == 0)
        {
            gtp_final_score(out_fp, idn);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "place_free_handicap") == 0)
        {
            gtp_place_free_handicap(out_fp, idn, args[0]);
            continue;
        }

        if(argc > 1 && strcmp(cmd, "set_free_handicap") == 0)
        {
            gtp_set_free_handicap(out_fp, idn, argc, args);
            continue;
        }

        if(argc == 3 && strcmp(cmd, "time_settings") == 0)
        {
            gtp_time_settings(out_fp, idn, args[0], args[1], args[2]);
            continue;
        }

        if(argc > 1 && argc < 5 && strcmp(cmd, "kgs-time_settings") == 0)
        {
            gtp_kgs_time_settings(out_fp, idn, args[0], args[1], args[2],
                args[3]);
            continue;
        }

        if(argc == 3 && strcmp(cmd, "time_left") == 0)
        {
            gtp_time_left(out_fp, idn, args[0], args[1], args[2]);
            continue;
        }

        if(argc == 0 && (strcmp(cmd, "cputime") == 0 || strcmp(cmd,
            "gomill-cpu_time") == 0))
        {
            gtp_cputime(out_fp, idn);
            continue;
        }

        if(strcmp(cmd, "echo") == 0)
        {
            gtp_echo(out_fp, idn, argc, args, false);
            continue;
        }

        if(strcmp(cmd, "echo_err") == 0)
        {
            gtp_echo(out_fp, idn, argc, args, true);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "mtld-last_evaluation") == 0)
        {
            gtp_last_evaluation(out_fp, idn);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "mtld-final_position") == 0)
        {
            gtp_final_position(out_fp, idn);
            continue;
        }

        if((argc == 1 || argc == 2) && strcmp(cmd, "loadsgf") == 0)
        {
            gtp_loadsgf(out_fp, idn, args[0], args[1]);
            continue;
        }

        if(argc <= 1 && strcmp(cmd, "printsgf") == 0)
        {
            gtp_printsgf(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "clear_cache") == 0)
        {
            gtp_clear_cache(out_fp, idn);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "gomill-describe_engine") == 0)
        {
            gtp_gomill_describe_engine(out_fp, idn);
            continue;
        }


        const char * best_dst_str = NULL;
        u16 best_dst_val = 0;
        bool command_exists = false;
        u16 i = 0;
        while(supported_commands[i] != NULL)
        {
            if(strcmp(cmd, supported_commands[i]) == 0)
            {
                command_exists = true;
                break;
            }
            else
            {
                u16 lev_dst = levenshtein_dst(supported_commands[i], cmd);
                if(best_dst_str == NULL || lev_dst < best_dst_val)
                {
                    best_dst_str = supported_commands[i];
                    best_dst_val = lev_dst;
                }
            }
            ++i;
        }

        if(command_exists)
        {
            fprintf(stderr, "warning: command '%s' exists but the parameter lis\
t is wrong; please check the documentation\n", cmd);
            error_msg(out_fp, idn, "syntax error");
        }
        else
        {
            if(best_dst_val < 2){
                strcpy(cmd, best_dst_str);
                goto cmd_matcher;
            }
            if(best_dst_val < 4)
                fprintf(stderr, "warning: command '%s' was not understood; did \
you mean '%s'?\n", cmd, best_dst_str);
            else
                fprintf(stderr, "warning: command '%s' was not understood; run \
\"help\" for a list of available commands\n", cmd);

            error_msg(out_fp, idn, "unknown command");
        }
    }
}
