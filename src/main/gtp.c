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
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/select.h> /* fd_set in macOS */

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
    "cputime",
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
    "mtld-game_info",
    "mtld-last_evaluation",
    "mtld-ponder",
    "mtld-review_game",
    "mtld-time_left",
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

extern bool time_system_overriden;
extern bool save_all_games_to_file;
extern bool resign_on_timeout;
extern bool pass_when_losing;
extern game_record current_game;
extern time_system current_clock_black;
extern time_system current_clock_white;
extern u32 limit_by_playouts;
extern char * sentinel_file;

static bool out_on_time_warning = false;

/*
These two fields are used to try to guess which player is the program. This is
only used for naming the players in SGF records.
*/
static bool has_genmoved_as_black = false;
static bool has_genmoved_as_white = false;

static u64 request_received_mark;

static out_board last_out_board;

extern clock_t start_cpu_time;

static void update_player_names()
{
    if(current_game.player_names_set)
        return;

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

static void gtp_error(
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

static void gtp_answer(
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
    gtp_answer(fp, id, "2");
}

static void gtp_name(
    FILE * fp,
    int id
){
    gtp_answer(fp, id, "matilda");
}

static void gtp_version(
    FILE * fp,
    int id
){
    char * s = alloc();
    version_string(s);
    gtp_answer(fp, id, s);
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
            gtp_answer(fp, id, "true");
            return;
        }
        ++i;
    }
    gtp_answer(fp, id, "false");
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

    gtp_answer(fp, id, buf);
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
    u32 seconds;
    if(!parse_uint(&seconds, timestr) || seconds < 1)
    {
        gtp_error(fp, id, "syntax error");
        return;
    }

    board current_state;
    current_game_state(&current_state, &current_game);
    bool is_black = current_player_color(&current_game);

    char * buf = alloc();

    request_opinion(buf, &current_state, is_black, seconds * 1000);

    gtp_answer(fp, id, buf);
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
    u32 seconds;
    if(!parse_uint(&seconds, timestr) || seconds < 1)
    {
        gtp_error(fp, id, "syntax error");
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
        evaluate_position_timed(&b, is_black, &out_b, stop_time,
            early_stop_time);

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
        just_play_slow(&b, is_black, actual);
        is_black = !is_black;
    }

    gtp_answer(fp, id, buf);
    release(buf);
}

static void gtp_quit(
    FILE * fp,
    int id
){
    gtp_answer(fp, id, NULL);
    exit(EXIT_SUCCESS);
}

static void gtp_clear_cache(
    FILE * fp,
    int id
){
    new_match_maintenance();
    gtp_answer(fp, id, NULL);
}

static void close_if_sentinel_found()
{
    if(sentinel_file == NULL)
        return;

    if(access(sentinel_file, F_OK) != 0)
        return;

    unlink(sentinel_file);
    flog_warn("gtp", "sentinel file found; closing");
    exit(EXIT_SUCCESS);
}

static void gtp_clear_board(
    FILE * fp,
    int id
){
    gtp_answer(fp, id, NULL);

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

static void gtp_kgs_game_over(
    FILE * fp,
    int id
){
    gtp_clear_board(fp, id);

    close_if_sentinel_found();
}

static void gtp_boardsize(
    FILE * fp,
    int id,
    const char * new_size
){
    if(new_size == NULL)
    {
        gtp_answer(fp, id, BOARD_SIZ_AS_STR);
        return;
    }

    u32 ns;
    if(!parse_uint(&ns, new_size))
    {
        gtp_error(fp, id, "syntax error");
        return;
    }

    if(ns != BOARD_SIZ)
    {
        gtp_error(fp, id, "unacceptable size");

        fprintf(stderr, "board size cannot be changed on runtime; please edit t\
he master header file and recompile matilda\n");
        char * s = alloc();
        snprintf(s, MAX_PAGE_SIZ, "requested board size change to %ux%u", ns,
            ns);
        flog_info("gtp", s);
        release(s);
    }
    else
        gtp_answer(fp, id, NULL);
}

static void gtp_komi(
    FILE * fp,
    int id,
    const char * new_komi
){
    if(new_komi == NULL)
    {
        char * kstr = alloc();
        komi_to_string(kstr, komi);
        gtp_answer(fp, id, kstr);
        release(kstr);
        return;
    }

    double komid;
    if(!parse_float(&komid, new_komi))
    {
        gtp_error(fp, id, "syntax error");
        return;
    }
    gtp_answer(fp, id, NULL);

    komi = (d16)(komid * 2.0);
}

static void gtp_play(
    FILE * fp,
    int id,
    const char * color,
    char * vertex,
    bool allow_skip
){
    bool is_black;
    if(!parse_color(&is_black, color))
    {
        gtp_error(fp, id, "syntax error");
        return;
    }

    if(allow_skip)
    {
        lower_case(vertex);
        if(strcmp(vertex, "skip") == 0)
        {
            add_play_out_of_order(&current_game, is_black, NONE);
            gtp_answer(fp, id, NULL);
            return;
        }
    }

    move m;
    if(!parse_gtp_vertex(vertex, &m))
    {
        gtp_error(fp, id, "syntax error");
        return;
    }

    if(m == NONE)
    {
        /* Resign */
        current_game.finished = true;
        current_game.resignation = true;
        current_game.final_score = is_black ? -1 : 1;
        gtp_answer(fp, id, NULL);
        return;
    }

    if(!play_is_legal(&current_game, is_black, m))
    {
        gtp_error(fp, id, "illegal move");
        return;
    }
    gtp_answer(fp, id, NULL);

    add_play_out_of_order(&current_game, is_black, m);
}

/*
Generic genmove functions that fulfills the needs of the GTP.
*/
static void generic_genmove(
    FILE * fp,
    int id,
    const char * color,
    bool commit_game_changes,
    bool allow_pass_when_winning
){
    bool is_black;
    if(!parse_color(&is_black, color))
    {
        gtp_error(fp, id, "syntax error");
        return;
    }

    char * buf = alloc();
    out_board out_b;

    if(commit_game_changes)
    {
        if(is_black)
            has_genmoved_as_black = true;
        else
            has_genmoved_as_white = true;
    }

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
    time_system * curr_clock = is_black ? &current_clock_black :
        &current_clock_white;

    /*
    Resign on timeout
    */
    if(resign_on_timeout && curr_clock->timed_out)
    {
        gtp_answer(fp, id, "resign");

        if(!out_on_time_warning)
        {
            out_on_time_warning = true;
            snprintf(buf, MAX_PAGE_SIZ, "matilda is believed to have lost on time");
            flog_warn("gtp", buf);
        }

        current_game.finished = true;
        current_game.resignation = true;
        current_game.final_score = is_black ? -1 : 1;
#if 0
        /* TODO just for counting resigns on timeout for paper */
        flog_debug("gtp", "TIMEOUT");
#endif
        close_if_sentinel_found();
        return;
    }

    bool has_play;
    if(limit_by_playouts > 0)
    {
        has_play = evaluate_position_sims(&current_state, is_black, &out_b,
            limit_by_playouts);
    }
    else
    {
        u16 stones = stone_count(current_state.p);
        time_to_play = calc_time_to_play(curr_clock, stones);
        if(time_to_play == UINT32_MAX)
            snprintf(buf, MAX_PAGE_SIZ, "time to play: infinite");
        else
        {
            char * s = alloc();
            format_nr_millis(s, time_to_play);
            snprintf(buf, MAX_PAGE_SIZ, "time to play: %s", s);
            release(s);
        }
        flog_info("gtp", buf);

        u64 stop_time = request_received_mark + time_to_play;
        u64 early_stop_time = request_received_mark + (time_to_play / 3);

        has_play = evaluate_position_timed(&current_state, is_black, &out_b,
            stop_time, early_stop_time);
    }

    memcpy(&last_out_board, &out_b, sizeof(out_board));

    move m;
    if(has_play)
    {
        /*
        A play or pass is suggested.
        */
        if(allow_pass_when_winning && out_b.pass >= JUST_PASS_WINRATE)
            m = PASS;
        else
            m = select_play(&out_b, is_black, &current_game);
    }
    else
    {
        /*
        The game is lost, a resign or pass is suggested.
        */
        if(pass_when_losing)
        {
            m = PASS;
        }
        else
        {
            gtp_answer(fp, id, "resign");

            snprintf(buf, MAX_PAGE_SIZ, "matilda playing as %s (%c) resigns\n",
                is_black ? "black" : "white", is_black ? BLACK_STONE_CHAR :
                WHITE_STONE_CHAR);
            flog_info("gtp", buf);

            if(commit_game_changes)
            {
                current_game.finished = true;
                current_game.resignation = true;
                current_game.final_score = is_black ? -1 : 1;
            }

            release(buf);

            close_if_sentinel_found();
            return;
        }
    }

    coord_to_gtp_vertex(buf, m);
    gtp_answer(fp, id, buf);

    if(commit_game_changes)
    {
        if(limit_by_playouts == 0)
        {
            u32 elapsed = (u32)(current_time_in_millis() -
                request_received_mark);

            advance_clock(curr_clock, elapsed);
        }

        /*
        Transpositions table maintenance
        */
        add_play_out_of_order(&current_game, is_black, m);
    }

    release(buf);
}

static void gtp_genmove(
    FILE * fp,
    int id,
    const char * color
){
    generic_genmove(fp, id, color, true, true);
}

static void gtp_genmove_cleanup(
    FILE * fp,
    int id,
    const char * color
){
    generic_genmove(fp, id, color, true, false);
}

static void gtp_reg_genmove(
    FILE * fp,
    int id,
    const char * color
){
    generic_genmove(fp, id, color, false, true);
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
    gtp_answer(fp, id, buf);

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
    if(time_system_overriden || limit_by_playouts > 0)
    {
        flog_warn("gtp", "attempt to set time settings ignored");
        gtp_answer(fp, id, NULL);
        return;
    }

    char * previous_ts_as_s = alloc();
    time_system_to_str(previous_ts_as_s, &current_clock_black);

    u32 new_main_time;
    u32 new_byo_yomi_time;
    u32 new_byo_yomi_stones;
    if(!parse_uint(&new_main_time, main_time) ||
        new_main_time > (UINT32_MAX / 1000))
    {
        gtp_error(fp, id, "syntax error");
        release(previous_ts_as_s);
        return;
    }
    if(!parse_uint(&new_byo_yomi_time, byo_yomi_time) ||
        new_byo_yomi_time > (UINT32_MAX / 1000))
    {
        gtp_error(fp, id, "syntax error");
        release(previous_ts_as_s);
        return;
    }
    if(!parse_uint(&new_byo_yomi_stones, byo_yomi_stones))
    {
        gtp_error(fp, id, "syntax error");
        release(previous_ts_as_s);
        return;
    }

    gtp_answer(fp, id, NULL);

    set_time_system(&current_clock_black, new_main_time * 1000,
        new_byo_yomi_time * 1000, new_byo_yomi_stones, 1);
    set_time_system(&current_clock_white, new_main_time * 1000,
        new_byo_yomi_time * 1000, new_byo_yomi_stones, 1);


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
    if(time_system_overriden || limit_by_playouts > 0)
    {
        flog_warn("gtp", "attempt to set time settings ignored");
        gtp_answer(fp, id, NULL);
        return;
    }

    if(systemstr == NULL)
    {
        gtp_error(fp, id, "syntax error");
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
            u32 new_main_time;
            if(main_time == NULL || !parse_uint(&new_main_time, main_time) ||
                new_main_time > (UINT32_MAX / 1000))
            {
                gtp_error(fp, id, "syntax error");
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

                u32 new_main_time;
                u32 new_byo_yomi_time;
                u32 new_byo_yomi_periods;
                if(main_time == NULL || !parse_uint(&new_main_time, main_time)
                    || new_main_time > (UINT32_MAX / 1000))
                {
                    gtp_error(fp, id, "syntax error");
                    release(previous_ts_as_s);
                    return;
                }
                if(byo_yomi_time == NULL || !parse_uint(&new_byo_yomi_time,
                    byo_yomi_time) ||
                    new_byo_yomi_time > (UINT32_MAX / 1000))
                {
                    gtp_error(fp, id, "syntax error");
                    release(previous_ts_as_s);
                    return;
                }
                if(byo_yomi_periods == NULL || !parse_uint(&new_byo_yomi_periods,
                    byo_yomi_periods))
                {
                    gtp_error(fp, id, "syntax error");
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
                    u32 new_main_time;
                    u32 new_byo_yomi_time;
                    u32 new_byo_yomi_stones;
                    if(main_time == NULL || !parse_uint(&new_main_time,
                        main_time) || new_main_time >= (UINT32_MAX / 1000))
                    {
                        gtp_error(fp, id, "syntax error");
                        release(previous_ts_as_s);
                        return;
                    }
                    if(byo_yomi_time == NULL || !parse_uint(&new_byo_yomi_time,
                        byo_yomi_time) ||
                        new_byo_yomi_time > (UINT32_MAX / 1000))
                    {
                        gtp_error(fp, id, "syntax error");
                        release(previous_ts_as_s);
                        return;
                    }
                    if(byo_yomi_stones == NULL ||
                        !parse_uint(&new_byo_yomi_stones, byo_yomi_stones))
                    {
                        gtp_error(fp, id, "syntax error");
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
                    gtp_error(fp, id, "syntax error");
                    release(previous_ts_as_s);
                    return;
                }

    gtp_answer(fp, id, NULL);

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

static void gtp_time_left_seconds(
    FILE * fp,
    int id,
    const char * color,
    const char * time_left, /* in seconds */
    const char * stones
){
    if(time_system_overriden || limit_by_playouts > 0)
    {
        flog_warn("gtp", "attempt to set time settings ignored");
        gtp_answer(fp, id, NULL);
        return;
    }

    bool is_black;
    if(!parse_color(&is_black, color))
    {
        gtp_error(fp, id, "syntax error");
        return;
    }
    u32 new_time_remaining;
    u32 new_byo_yomi_stones_remaining;
    if(!parse_uint(&new_time_remaining, time_left))
    {
        gtp_error(fp, id, "syntax error");
        return;
    }
    if(!parse_uint(&new_byo_yomi_stones_remaining, stones))
    {
        gtp_error(fp, id, "syntax error");
        return;
    }

    gtp_answer(fp, id, NULL);


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

static void gtp_time_left_millis(
    FILE * fp,
    int id,
    const char * color,
    const char * time_left, /* in milliseconds */
    const char * stones
){
    if(time_system_overriden || limit_by_playouts > 0)
    {
        flog_warn("gtp", "attempt to set time settings ignored");
        gtp_answer(fp, id, NULL);
        return;
    }

    bool is_black;
    if(!parse_color(&is_black, color))
    {
        gtp_error(fp, id, "syntax error");
        return;
    }
    u32 new_time_remaining;
    u32 new_byo_yomi_stones_remaining;
    if(!parse_uint(&new_time_remaining, time_left))
    {
        gtp_error(fp, id, "syntax error");
        return;
    }
    if(!parse_uint(&new_byo_yomi_stones_remaining, stones))
    {
        gtp_error(fp, id, "syntax error");
        return;
    }

    gtp_answer(fp, id, NULL);


    time_system * curr_clock = is_black ? &current_clock_black :
        &current_clock_white;

    if(new_byo_yomi_stones_remaining == 0)
    {
        /* Main time is still counting down */
        curr_clock->main_time_remaining = new_time_remaining;
    }
    else
    {
        /* Byo-yomi time */
        curr_clock->byo_yomi_time_remaining = new_time_remaining;
        curr_clock->byo_yomi_stones_remaining = new_byo_yomi_stones_remaining;
    }
}

static void gtp_cputime(
    FILE * fp,
    int id
){
    double elapsed = ((double)(clock() - start_cpu_time)) / ((double)CLOCKS_PER_SEC);

    char * buf = alloc();
    snprintf(buf, MAX_PAGE_SIZ, "%.3f", elapsed);
    gtp_answer(fp, id, buf);
    release(buf);
}

static void gtp_final_status_list(
    FILE * fp,
    int id,
    const char * status
){
    if(strcmp(status, "alive") == 0)
    {
        char * buf = alloc();
        char * mstr = alloc();
        d32 idx = 0;

        board current_state;
        current_game_state(&current_state, &current_game);
        for(move m = 0; m < TOTAL_BOARD_SIZ; ++m)
        {
            coord_to_alpha_num(mstr, m);
            idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "%s\n",
                mstr);
        }
        gtp_answer(fp, id, buf);
        release(mstr);
        release(buf);
        return;
    }

    if(strcmp(status, "dead") == 0 || strcmp(status, "seki") == 0)
    {
        gtp_answer(fp, id, NULL);
        return;
    }

    gtp_error(fp, id, "syntax error");
}

static void gtp_gomill_describe_engine(
    FILE * fp,
    int id
){
    char * s = alloc();
    build_info(s);
    gtp_answer(fp, id, s);
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
    gtp_answer(fp, id, str);

    release(str2);
    release(str);
}

static void gtp_game_info(
    FILE * fp,
    int id
){
    char * s = malloc(MAX_FILE_SIZ);
    if(s == NULL)
        flog_crit("gtp", "system out of memory");
    game_record_to_string(s, MAX_FILE_SIZ, &current_game);
    gtp_answer(fp, id, s);
    free(s);
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
    int id,
    const char * number
){
    u32 moves;
    if(number == NULL)
        moves = 1;
    else
        if(!parse_uint(&moves, number) || moves < 1)
        {
            gtp_error(fp, id, "syntax error");
            return;
        }

    if(generic_undo(moves))
        gtp_answer(fp, id, NULL);
    else
        gtp_error(fp, id, "cannot undo");
}

static void gtp_last_evaluation(
    FILE * fp,
    int id
){
    char * s = alloc();
    s[0] = '\n';
    out_board_to_string(s + 1, &last_out_board);
    gtp_answer(fp, id, s);
    release(s);
}

static void gtp_final_score(
    FILE * fp,
    int id
){
    board current_state;
    current_game_state(&current_state, &current_game);
    d16 score = score_stones_and_area(current_state.p);

    current_game.finished = true;
    current_game.final_score = score;

    char * s = alloc();
    score_to_string(s, score);
    gtp_answer(fp, id, s);
    release(s);

    close_if_sentinel_found();
}

static void gtp_place_free_handicap(
    FILE * fp,
    int id,
    const char * nstones
){
    u32 num_stones;
    if(!parse_uint(&num_stones, nstones) || num_stones < 1)
    {
        gtp_error(fp, id, "syntax error");
        return;
    }
    board current_state;
    current_game_state(&current_state, &current_game);
    if(stone_count(current_state.p) > 0)
    {
        gtp_error(fp, id, "board is not empty");
        return;
    }
    if(num_stones < 2 || num_stones > TOTAL_BOARD_SIZ - 2)
    {
        gtp_error(fp, id, "invalid number of stones");
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

    gtp_answer(fp, id, buf);

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
        gtp_error(fp, id, "board is not empty");
        return;
    }
    if(num_vertices < 2 || num_vertices > TOTAL_BOARD_SIZ - 1)
    {
        gtp_error(fp, id, "bad vertex list");
        return;
    }
    for(u16 v = 0; v < num_vertices; ++v)
    {
        move m;
        if(!parse_gtp_vertex(vertices[v], &m) || m == PASS)
        {
            gtp_error(fp, id, "bad vertex list");
            return;
        }
        if(!add_handicap_stone(&current_game, m))
        {
            gtp_error(fp, id, "bad vertex list");
            return;
        }
    }

    gtp_answer(fp, id, NULL);
}

static void gtp_loadsgf(
    FILE * fp,
    int id,
    const char * filename,
    const char * move_number /* optional */
){
    if(!validate_filename(filename))
    {
        gtp_error(fp, id, "cannot load file");
        return;
    }

    u32 move_until;
    if(move_number == NULL)
        move_until = MAX_GAME_LENGTH;
    else
        if(!parse_uint(&move_until, move_number) || move_until < 1)
        {
            gtp_error(fp, id, "syntax error");
            return;
        }

    char * filepath = alloc();
    snprintf(filepath, MAX_PAGE_SIZ, "%s%s", data_folder(), filename);

    game_record tmp;
    reset_warning_messages();
    bool imported = import_game_from_sgf(&tmp, filepath);
    if(!imported)
    {
        gtp_error(fp, id, "cannot load file");
        release(filepath);
        return;
    }

    gtp_answer(fp, id, NULL);

    tmp.turns = MIN(tmp.turns, move_until - 1);

    memcpy(&current_game, &tmp, sizeof(game_record));

    release(filepath);
}

static void gtp_printsgf(
    FILE * fp,
    int id,
    const char * filename
){
    update_player_names();

    char * buf = alloc();

    if(filename == NULL || strcmp(filename, "-") == 0)
    {
        export_game_as_sgf_to_buffer(&current_game, buf, MAX_PAGE_SIZ);
        gtp_answer(fp, id, buf);
    }
    else
    {
        if(!validate_filename(filename))
        {
            gtp_error(fp, id, "illegal file name");
            release(buf);
            return;
        }

        snprintf(buf, MAX_PAGE_SIZ, "%s%s", data_folder(), filename);

        bool success = export_game_as_sgf(&current_game, buf);
        if(success)
            gtp_answer(fp, id, NULL);
        else
            gtp_error(fp, id, "could not create file");
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
    tt_init();

    flog_info("gtp", "matilda now running over GTP");
    char * s = alloc();
    build_info(s);
    flog_debug("gtp", s);
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

        if(think_in_opt_turn)
        {
            do{
                FD_ZERO(&readfs);
                FD_SET(STDIN_FILENO, &readfs);
                struct timeval tm;
                tm.tv_sec = 0;
                tm.tv_usec = 2000;

                int ready = select(STDIN_FILENO + 1, &readfs, NULL, NULL, &tm);
                if(ready == 0) /* nothing to read */
                    evaluate_in_background(&current_state, is_black);
                else
                    break;
            }while(1);
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
        if(parse_int(&idn, id))
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

lbl_parse_command:
        /*
        Commands more commonly used should be parsed first:
        */
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

        if(argc == 3 && strcmp(cmd, "time_left") == 0)
        {
            gtp_time_left_seconds(out_fp, idn, args[0], args[1], args[2]);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "reg_genmove") == 0)
        {
            gtp_reg_genmove(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "clear_board") == 0)
        {
            gtp_clear_board(out_fp, idn);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "kgs-game_over") == 0)
        {
            gtp_kgs_game_over(out_fp, idn);
            continue;
        }

        if(argc <= 1 && strcmp(cmd, "komi") == 0)
        {
            gtp_komi(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "kgs-genmove_cleanup") == 0)
        {
            gtp_genmove_cleanup(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "final_status_list") == 0)
        {
            gtp_final_status_list(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 3 && strcmp(cmd, "mtld-time_left") == 0)
        {
            gtp_time_left_millis(out_fp, idn, args[0], args[1], args[2]);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "undo") == 0)
        {
            gtp_undo(out_fp, idn, NULL);
            continue;
        }

        if(argc <= 1 && strcmp(cmd, "gg-undo") == 0)
        {
            gtp_undo(out_fp, idn, args[0]);
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

        if(argc <= 1 && strcmp(cmd, "boardsize") == 0)
        {
            gtp_boardsize(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "showboard") == 0)
        {
            gtp_showboard(out_fp, idn);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "final_score") == 0)
        {
            gtp_final_score(out_fp, idn);
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

        if(argc == 0 && strcmp(cmd, "cputime") == 0)
        {
            gtp_cputime(out_fp, idn);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "gomill-cpu_time") == 0)
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

        if(argc == 0 && strcmp(cmd, "mtld-game_info") == 0)
        {
            gtp_game_info(out_fp, idn);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "gomill-describe_engine") == 0)
        {
            gtp_gomill_describe_engine(out_fp, idn);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "quit") == 0)
        {
            gtp_quit(out_fp, idn);
            continue;
        }

        if(argc == 0 && strcmp(cmd, "exit") == 0)
        {
            gtp_quit(out_fp, idn);
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
            gtp_error(out_fp, idn, "syntax error");
        }
        else
        {
            if(best_dst_val < 2){
                strcpy(cmd, best_dst_str);
                goto lbl_parse_command;
            }
            if(best_dst_val < 4)
                fprintf(stderr, "warning: command '%s' does not exist; did you \
mean '%s'?\n", cmd, best_dst_str);
            else
                fprintf(stderr, "warning: command '%s' does not exist; run \"he\
lp\" for a list of available commands\n", cmd);

            gtp_error(out_fp, idn, "unknown command");
        }
    }
}
