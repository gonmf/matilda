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

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "analysis.h"
#include "board.h"
#include "engine.h"
#include "file_io.h"
#include "flog.h"
#include "types.h"
#include "game_record.h"
#include "pts_file.h"
#include "opening_book.h"
#include "randg.h"
#include "random_play.h"
#include "scoring.h"
#include "sgf.h"
#include "state_changes.h"
#include "stringm.h"
#include "timem.h"
#include "time_ctrl.h"
#include "transpositions.h"
#include "buffer.h"

extern s16 komi;
extern u32 network_roundtrip_delay;
extern bool network_round_trip_set;
extern float frisbee_prob;

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
#if ENABLE_FRISBEE_GO
    "frisbee-accuracy",
    "frisbee-epsilon",
    "frisbee-play",
    "frisbee-reg_genmove",
#endif
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
    char * buf = get_buffer();
    if(id == -1)
        snprintf(buf, MAX_PAGE_SIZ, "? %s\n\n", s);
    else
        snprintf(buf, MAX_PAGE_SIZ, "?%d %s\n\n", id, s);

    size_t w = fwrite(buf, 1, strlen(buf), fp);
    if(w != strlen(buf))
        flog_crit("gtp", "failed to write to comm. file descriptor");

    fflush(fp);

    flog_prot("gtp", buf);
}

static void answer_msg(
    FILE * fp,
    int id,
    const char * s
){
    char * buf = get_buffer();
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
    char buf[32];
    snprintf(buf, 32, "%u.%u", VERSION_MAJOR, VERSION_MINOR);
    answer_msg(fp, id, buf);
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
    char * buf = get_buffer();
    u16 idx = 0;
    u16 i = 0;
    while(supported_commands[i] != NULL)
    {
        strcpy(buf + idx, supported_commands[i]);
        idx += strlen(supported_commands[i]);
        if(supported_commands[i + 1] != NULL)
        {
            strcpy(buf + idx, "\n");
            idx++;
        }
        ++i;
    }
    answer_msg(fp, id, buf);
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
    s32 seconds;
    if(!parse_int(timestr, &seconds) || seconds < 1)
    {
        error_msg(fp, id, "syntax error");
        return;
    }

    board * current_state = current_game_state(&current_game);
    bool is_black = current_player_color(&current_game);

    char * buf = get_buffer();

    request_opinion(buf, current_state, is_black, seconds * 1000);

    answer_msg(fp, id, buf);
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
    s32 seconds;
    if(!parse_int(timestr, &seconds) || seconds < 1)
    {
        error_msg(fp, id, "syntax error");
        return;
    }

    u32 idx = 0;
    char * buf = get_buffer();

    new_match_maintenance();

    out_board out_b;
    board * t = first_game_state(&current_game);
    board b;
    memcpy(&b, t, sizeof(board));
    bool is_black = first_player_color(&current_game);

    for(u16 t = 0; t < current_game.turns; ++t)
    {
        u64 curr_time = current_time_in_millis();
        u64 stop_time = curr_time + seconds * 1000;
        u64 early_stop_time = curr_time + seconds * 500;
        evaluate_position(&b, is_black, &out_b, stop_time, early_stop_time);

        move best = select_play_fast(&out_b);
        move actual = current_game.moves[t];
        if(is_board_move(actual))
            idx += snprintf(buf + idx, 4 * 1024 - idx,
                "%u: (%c) Actual: %s (%.3f)", t, is_black ? 'B' : 'W',
                coord_to_alpha_num(actual), out_b.value[actual]);
        else
            idx += snprintf(buf + idx, 4 * 1024 - idx,
                "%u: (%c) Actual: pass", t, is_black ? 'B' : 'W');
        if(is_board_move(best))
            idx += snprintf(buf + idx, 4 * 1024 - idx,
                " Best: %s (%.3f)\n", coord_to_alpha_num(best),
                out_b.value[best]);
        else
            idx += snprintf(buf + idx, 4 * 1024 - idx, " Best: pass\n");
        opt_turn_maintenance(&b, is_black);
        just_play_slow(&b, actual, is_black);
        is_black = !is_black;
    }

    answer_msg(fp, id, buf);
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
    if(save_all_games_to_file && current_game.turns > 0)
    {
        update_player_names();
        char * filename = get_buffer();
        if(export_game_as_sgf_auto_named(&current_game, filename))
        {
            char * buf = get_buffer();
            snprintf(buf, MAX_PAGE_SIZ, "game record exported to %s", filename);
            flog_info("gtp", buf);
        }
        else
            flog_warn("gtp", "failed to export game record to file");
    }

    has_genmoved_as_black = false;
    has_genmoved_as_white = false;
    clear_game_record(&current_game);
    new_match_maintenance();
    reset_clock(&current_clock_black);
    reset_clock(&current_clock_white);
    out_on_time_warning = false;
    answer_msg(fp, id, NULL);
}

static void gtp_boardsize(
    FILE * fp,
    int id,
    const char * new_size
){
    s32 ns;
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
    if(parse_float(new_komi, &komid))
    {
        s16 komi2 = (s16)(komid * 2.0);
        if(komi != komi2)
        {
            fprintf(stderr, "komidashi changed from %s to %s stones\n",
                komi_to_string(komi), komi_to_string(komi2));
            komi = komi2;
        }
        else
            fprintf(stderr, "komidashi kept at %s stones\n",
                komi_to_string(komi));

        answer_msg(fp, id, NULL);
    }
    else
        error_msg(fp, id, "syntax error");
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
            board * current_state = current_game_state(&current_game);
            opt_turn_maintenance(current_state, !is_black);
            answer_msg(fp, id, NULL);
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

    add_play_out_of_order(&current_game, is_black, m);
    current_game.game_finished = false;
    board * current_state = current_game_state(&current_game);
    opt_turn_maintenance(current_state, !is_black);
    answer_msg(fp, id, NULL);
}

/*
Generic genmove functions that fulfills the needs of the GTP plus a non-standard
KGS Go Server command.
*/
static void generic_genmove(
    FILE * fp,
    int id,
    const char * color,
    bool reg,
    bool kill_dead_groups
){
    bool is_black;
    if(parse_color(color, &is_black))
    {
        out_board out_b;

        if(is_black)
            has_genmoved_as_black = true;
        else
            has_genmoved_as_white = true;

        board * current_state = current_game_state(&current_game);

        /*
        We may be asked to play with the same color two times in a row.
        This may trigger false ko violations; so we prevent them here.
        */
        if(current_game.turns > 0 && current_player_color(&current_game) !=
            is_black)
        {
            current_state->last_played = NONE;
            current_state->last_eaten = NONE;
        }

        u32 time_to_play = 0;

#if !LIMIT_BY_PLAYOUTS
        time_system * curr_clock = is_black ? &current_clock_black :
            &current_clock_white;

        u16 stones = stone_count(current_state->p);
        time_to_play = calc_time_to_play(curr_clock, stones);

        char * buf = get_buffer();
        snprintf(buf, MAX_PAGE_SIZ, "time to play: %u.%03us\n", time_to_play /
            1000, time_to_play % 1000);
        flog_info("gtp", buf);
#endif

        u64 stop_time = request_received_mark + time_to_play;
        u64 early_stop_time = request_received_mark + (time_to_play / 2);
        bool has_play = evaluate_position(current_state, is_black, &out_b,
            stop_time, early_stop_time);

        memcpy(&last_out_board, &out_b, sizeof(out_board));

        /*
        If strategy suggests resigning then go ahead.
        This is not the same as passing when it is the best play.
        Sometimes it is also not adviseable to pass instead of killing all
        groups.
        */
        if(!has_play)
        {
#if CAN_RESIGN
            answer_msg(fp, id, "resign");
            char * buf = get_buffer();
            snprintf(buf, MAX_PAGE_SIZ, "matilda playing as %s (%c) resigns\n",
                is_black ? "black" : "white", is_black ? BLACK_STONE_CHAR :
                WHITE_STONE_CHAR);
            flog_warn("gtp", buf);
            current_game.game_finished = true;
            current_game.resignation = true;
            current_game.final_score = is_black ? -1 : 1;
            return;
#endif
            if(kill_dead_groups)
                random_play(current_state, is_black, &out_b);
        }

        current_state = current_game_state(&current_game);
        move m = select_play(&out_b, is_black, &current_game);

        if(m == PASS && kill_dead_groups)
        {
            random_play(current_state, is_black, &out_b);
            m = select_play(&out_b, is_black, &current_game);
        }

        if(m != PASS && !can_play_slow(current_state, m, is_black))
            flog_crit("gtp", "best evaluated play is illegal");

        answer_msg(fp, id, coord_to_gtp_vertex(m));

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
                    char * buf = get_buffer();
                    snprintf(buf, MAX_PAGE_SIZ, "matilda playing as %s (%c) res\
igns because of timeout\n", is_black ? "black" : "white", is_black ?
                        BLACK_STONE_CHAR : WHITE_STONE_CHAR);
                    flog_warn("gtp", buf);
                    current_game.game_finished = true;
                    current_game.resignation = true;
                    current_game.final_score = is_black ? -1 : 1;




#if 0

                    /* TODO just for counting resigns on timeout for paper */
                    flog_dbug("gtp", "TIMEDOUT\n");


#endif





                    return;
                }
                if(!out_on_time_warning)
                {
                    out_on_time_warning = true;
                    char * buf = get_buffer();
                    snprintf(buf, MAX_PAGE_SIZ, "matilda is believed to have lo\
st on time");
                    flog_warn("gtp", buf);
                }
                /* we don't do anything else when timed out */
            }
#endif

            if(ENABLE_FRISBEE_GO && frisbee_prob < 1.0)
                flog_crit("gtp", "playing Frisbee Go but play modification ha\
s been ignored by invoking genmove");
        }
    }
    else
        error_msg(fp, id, "syntax error");
}

static void gtp_genmove(
    FILE * fp,
    int id,
    const char * color
){
    generic_genmove(fp, id, color, false, false);
}

static void gtp_reg_genmove(
    FILE * fp,
    int id,
    const char * color
){
    generic_genmove(fp, id, color, true, false);
}

static void gtp_kgs_genmove_cleanup(
    FILE * fp,
    int id,
    const char * color
){
    generic_genmove(fp, id, color, true, true);
}

static void gtp_echo(
    FILE * fp,
    int id,
    u16 argc,
    char * argv[],
    bool print_to_stderr
){
    char * buf = get_buffer();
    s32 idx = 0;
    if(argc > 0)
        idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, "%s", argv[0]);

    for(u16 k = 1; k < argc; ++k)
        idx += snprintf(buf + idx, MAX_PAGE_SIZ - idx, " %s", argv[k]);

    if(print_to_stderr)
        fprintf(stderr, "%s\n", buf);
    answer_msg(fp, id, buf);
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

    const char * previous_ts_as_s = time_system_to_str(&current_clock_black);

    s32 new_main_time;
    s32 new_byo_yomi_time;
    s32 new_byo_yomi_stones;
    if(!parse_int(main_time, &new_main_time) || new_main_time < 0 ||
        new_main_time >= 2147484)
    {
        error_msg(fp, id, "syntax error");
        return;
    }
    if(!parse_int(byo_yomi_time, &new_byo_yomi_time) || new_byo_yomi_time < 0 ||
        new_byo_yomi_time >= 2147484)
    {
        error_msg(fp, id, "syntax error");
        return;
    }
    if(!parse_int(byo_yomi_stones, &new_byo_yomi_stones) || new_byo_yomi_stones
        < 0)
    {
        error_msg(fp, id, "syntax error");
        return;
    }

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

    const char * new_ts_as_s = time_system_to_str(&current_clock_black);

    char * buf = get_buffer();
    if(strcmp(previous_ts_as_s, new_ts_as_s) == 0)
        snprintf(buf, MAX_PAGE_SIZ, "clock settings kept at %s for both p\
layers", previous_ts_as_s);
    else
        snprintf(buf, MAX_PAGE_SIZ, "clock settings changed from %s to %s\
 for both players", previous_ts_as_s, new_ts_as_s);

    flog_info("gtp", buf);

    answer_msg(fp, id, NULL);
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

    const char * previous_ts_as_s = time_system_to_str(&current_clock_black);

    if(strcmp(systemstr, "none") == 0)
    {
        set_time_per_turn(&current_clock_black, DEFAULT_TIME_PER_TURN);
        set_time_per_turn(&current_clock_white, DEFAULT_TIME_PER_TURN);
    }
    else
        if(strcmp(systemstr, "absolute") == 0)
        {
            s32 new_main_time;
            if(main_time == NULL || !parse_int(main_time, &new_main_time) ||
                new_main_time < 0 || new_main_time >= 2147484)
            {
                error_msg(fp, id, "syntax error");
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

                s32 new_main_time;
                s32 new_byo_yomi_time;
                s32 new_byo_yomi_periods;
                if(main_time == NULL || !parse_int(main_time, &new_main_time) ||
                    new_main_time < 0 || new_main_time >= 2147484)
                {
                    error_msg(fp, id, "syntax error");
                    return;
                }
                if(byo_yomi_time == NULL || !parse_int(byo_yomi_time,
                    &new_byo_yomi_time) || new_byo_yomi_time < 0 ||
                    new_byo_yomi_time >= 2147484)
                {
                    error_msg(fp, id, "syntax error");
                    return;
                }
                if(byo_yomi_periods == NULL || !parse_int(byo_yomi_periods,
                    &new_byo_yomi_periods) || new_byo_yomi_periods < 0)
                {
                    error_msg(fp, id, "syntax error");
                    return;
                }

                set_time_system(&current_clock_black, new_main_time * 1000,
                    new_byo_yomi_time * 1000, 1, new_byo_yomi_periods);
                set_time_system(&current_clock_white, new_main_time * 1000,
                    new_byo_yomi_time * 1000, 1, new_byo_yomi_periods);
            }else
                if(strcmp(systemstr, "canadian") == 0)
                {
                    s32 new_main_time;
                    s32 new_byo_yomi_time;
                    s32 new_byo_yomi_stones;
                    if(main_time == NULL || !parse_int(main_time,
                        &new_main_time) || new_main_time < 0 || new_main_time >=
                        2147484)
                    {
                        error_msg(fp, id, "syntax error");
                        return;
                    }
                    if(byo_yomi_time == NULL || !parse_int(byo_yomi_time,
                        &new_byo_yomi_time) || new_byo_yomi_time < 0 ||
                        new_byo_yomi_time >= 2147484)
                    {
                        error_msg(fp, id, "syntax error");
                        return;
                    }
                    if(byo_yomi_stones == NULL || !parse_int(byo_yomi_stones,
                        &new_byo_yomi_stones) || new_byo_yomi_stones < 0)
                    {
                        error_msg(fp, id, "syntax error");
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
                    return;
                }

    const char * new_ts_as_s = time_system_to_str(&current_clock_black);

    char * buf = get_buffer();
    if(strcmp(previous_ts_as_s, new_ts_as_s) == 0)
        snprintf(buf, MAX_PAGE_SIZ,
            "clock settings kept at %s for both players", previous_ts_as_s);
    else
        snprintf(buf, MAX_PAGE_SIZ, "clock settings changed from %s to %s for b\
oth players", previous_ts_as_s, new_ts_as_s);

    flog_info("gtp", buf);

    answer_msg(fp, id, NULL);
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
    s32 new_time_remaining;
    s32 new_byo_yomi_stones_remaining;
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

    answer_msg(fp, id, NULL);
}

static void gtp_cputime(
    FILE * fp,
    int id
){
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

    char * buf = get_buffer();
    snprintf(buf, MAX_PAGE_SIZ, "%u.%03lu", (u32)ts.tv_sec, ((u64)ts.tv_nsec) /
        1000000);

    answer_msg(fp, id, buf);
}

static void gtp_final_status_list(
    FILE * fp,
    int id,
    const char * status
){
    char * buf = get_buffer();
    s32 sz = MAX_PAGE_SIZ;
    buf[0] = 0;
    s32 pos = 0;

    bool is_black = current_player_color(&current_game);
    board * current_state = current_game_state(&current_game);

    u8 e[BOARD_SIZ * BOARD_SIZ];
    estimate_final_position(current_state, is_black, e);

    if(strcmp(status, "dead") == 0)
    {
        for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
            if(current_state->p[m] != EMPTY && e[m] != current_state->p[m])
                pos += snprintf(buf + pos, sz - pos, "%s\n",
                    coord_to_alpha_num(m));
        answer_msg(fp, id, buf);
    }
    else
        if(strcmp(status, "alive") == 0)
        {
            for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
                if(current_state->p[m] != EMPTY && e[m] == current_state->p[m])
                    pos += snprintf(buf + pos, sz - pos, "%s\n",
                        coord_to_alpha_num(m));
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
}

static void gtp_gomill_describe_engine(
    FILE * fp,
    int id
){
    answer_msg(fp, id, build_info());
}

static void gtp_showboard(
    FILE * fp,
    int id
){
    board * b = current_game_state(&current_game);
    answer_msg(fp, id, board_to_string(b->p, b->last_played, b->last_eaten));
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

static void gtp_frisbee_accuracy(
    FILE * fp,
    int id,
    const char * floatv
){
    double v;
    if(!parse_float(floatv, &v))
    {
        error_msg(fp, id, "syntax error");
        return;
    }

    if(v < 0.0 || v > 1.0)
    {
        error_msg(fp, id, "syntax error");
        return;
    }

    if(current_game.turns > 0)
    {
        error_msg(fp, id, "unable to change");
        flog_warn("gtp", "unable to change frisbee accuracy midgame");
        return;
    }

    char * buf = get_buffer();
    if(v == frisbee_prob)
        snprintf(buf, MAX_PAGE_SIZ, "frisbee accuracy kept at %.2f",
            frisbee_prob);
    else
    {
        snprintf(buf, MAX_PAGE_SIZ,
            "changed frisbee accuracy from %.2f to %.2f", frisbee_prob, v);
        frisbee_prob = v;
    }

    flog_info("gtp", buf);
    answer_msg(fp, id, NULL);
}

static void gtp_undo_multiple(
    FILE * fp,
    int id,
    const char * number
){
    s32 moves;
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
    answer_msg(fp, id, out_board_to_string(&last_out_board));
}

static void gtp_final_position(
    FILE * fp,
    int id
){
    board * current_state = current_game_state(&current_game);
    bool is_black = current_player_color(&current_game);

    board e;
    e.last_eaten = e.last_played = NONE;
    estimate_final_position(current_state, is_black, e.p);

    answer_msg(fp, id, board_to_string(e.p, e.last_played, e.last_eaten));
}

static void gtp_final_score(
    FILE * fp,
    int id
){
    s16 score;
    if(estimate_score)
    {
        board * current_state = current_game_state(&current_game);
        bool is_black = current_player_color(&current_game);
        score = score_estimate(current_state, is_black);
    }else
        score = 0;

    current_game.game_finished = true;
    current_game.final_score = score;

    answer_msg(fp, id, score_to_string(score));
}

static void gtp_place_free_handicap(
    FILE * fp,
    int id,
    const char * nstones
){
    s32 num_stones;
    if(!parse_int(nstones, &num_stones) || num_stones < 1)
    {
        error_msg(fp, id, "syntax error");
        return;
    }
    board * current_state = current_game_state(&current_game);
    if(stone_count(current_state->p) > 0)
    {
        error_msg(fp, id, "board is not empty");
        return;
    }
    if(num_stones < 2 || num_stones > BOARD_SIZ * BOARD_SIZ - 1)
    {
        error_msg(fp, id, "invalid number of stones");
        return;
    }

    char * buf = malloc(MAX_PAGE_SIZ);
    if(buf == NULL)
        flog_crit("gtp", "system out of memory");

    char * b2 = buf;

    move_seq handicaps;
    get_ordered_handicap(&handicaps);
    for(move i = 0; i < handicaps.count && num_stones > 0; ++i)
    {
        move m = handicaps.coord[i];
        if(!add_handicap_stone(&current_game, m))
            flog_crit("gtp", "add handicap stone failed (1)");

        --num_stones;
        b2 += snprintf(b2, 8, "%s ", coord_to_alpha_num(m));
    }

    /*
    If the user wants even more handicap stones, choose randomly
    */
    while(num_stones > 0)
    {
        u8 x = rand_u16(BOARD_SIZ);
        u8 y = rand_u16(BOARD_SIZ);
        if((x == 0 || y == 0 || x == BOARD_SIZ - 1 || y == BOARD_SIZ - 1) &&
            rand_u16(10) > 0)
            continue;
        board * current_state = current_game_state(&current_game);
        move m = coord_to_move(x, y);
        if(current_state->p[m] == EMPTY)
        {
            if(!add_handicap_stone(&current_game, m))
                flog_crit("gtp", "add handicap stone failed (2)");

            --num_stones;
            b2 += snprintf(b2, 8, "%s ", coord_to_alpha_num(m));
        }
    }

    answer_msg(fp, id, buf);

    free(buf);
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
    if(num_vertices < 2 || num_vertices > BOARD_SIZ * BOARD_SIZ - 1)
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

    s32 move_until;
    if(move_number == NULL)
        move_until = MAX_GAME_LENGTH;
    else
        if(!parse_int(move_number, &move_until) || move_until < 1)
        {
            error_msg(fp, id, "syntax error");
            return;
        }

    char * buf = get_buffer();
    snprintf(buf, MAX_PAGE_SIZ, "%s%s", get_data_folder(), filename);

    game_record tmp;

    bool imported = import_game_from_sgf(&tmp, buf);
    if(!imported)
    {
        error_msg(fp, id, "cannot load file");
        return;
    }

    tmp.turns = MIN(tmp.turns, move_until - 1);

    memcpy(&current_game, &tmp, sizeof(game_record));

    answer_msg(fp, id, NULL);
}

static void gtp_printsgf(
    FILE * fp,
    int id,
    const char * filename
){
    update_player_names();

    if(filename == NULL)
    {
        char * buf = get_buffer();
        export_game_as_sgf_to_buffer(&current_game, buf, MAX_PAGE_SIZ);
        answer_msg(fp, id, buf);
    }
    else
    {
        if(!validate_filename(filename))
        {
            fprintf(stderr, "illegal file name\n");
            error_msg(fp, id, "cannot save file");
            return;
        }

        char * buf = get_buffer();
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

    flog_info("gtp", "matilda now running over GTP");
    flog_info("gtp", build_info());

    if(ENABLE_FRISBEE_GO && frisbee_prob < 1.0)
        flog_warn("gtp", "while playing Frisbee Go in GTP mode it is assumed th\
e plays are modified randomly by the controller or adapter program; prior to pl\
ay commands and after reg_genmove commands. Do not invoke genmove commands inst\
ead.");

    FILE * out_fp;
    int _out_fp = dup(STDOUT_FILENO);
    if(_out_fp == -1)
        flog_crit("gtp", "file descriptor duplication\n");

    close(STDOUT_FILENO);
    out_fp = fdopen(_out_fp, "w");
    assert(out_fp != NULL);

    clear_out_board(&last_out_board);
    clear_game_record(&current_game);

#if DETECT_NETWORK_LATENCY
    u64 last_time_frame = 0;
    bool time_frame_set = false;
#endif

    while(1)
    {
        bool is_black = current_player_color(&current_game);

        board current_state;
        memcpy(&current_state, current_game_state(&current_game),
            sizeof(board));

        while(1)
        {
            if(think_in_opt_turn && current_state.last_played != NONE)
            {
                fd_set readfs;
                FD_ZERO(&readfs);
                FD_SET(STDIN_FILENO, &readfs);
                struct timeval tm;
                tm.tv_sec = 0;
                tm.tv_usec = 2000;

                int ready = select(STDIN_FILENO + 1, &readfs, NULL, NULL, &tm);
                if(ready == 0)
                {
                    mcts_resume(&current_state, is_black);
                    continue;
                }
            }
            break;
        }

        opt_turn_maintenance(&current_state, is_black);
        reset_mcts_can_resume();

        char * buf = get_buffer();
        char * line = fgets(buf, MAX_PAGE_SIZ, stdin);
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

        line = trim(buf);
        if(line == NULL)
            continue;

        flog_prot("gtp", line);

        char * save_ptr;
        char * id = strtok_r(line, " |", &save_ptr);
        s32 idn;
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
        char * args[BOARD_SIZ * BOARD_SIZ];
        for(u16 i = 0; i < BOARD_SIZ * BOARD_SIZ; ++i)
        {
            args[i] = strtok_r(NULL, " |", &save_ptr);
            if(args[i] == NULL)
            {
                ++i;
                for(; i < BOARD_SIZ * BOARD_SIZ; ++i)
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

        if(argc == 2 && (ENABLE_FRISBEE_GO && strcmp(cmd, "frisbee-play") == 0))
        {
            gtp_play(out_fp, idn, args[0], args[1], true);
            continue;
        }

        if(argc == 1 && strcmp(cmd, "genmove") == 0)
        {
            gtp_genmove(out_fp, idn, args[0]);
            continue;
        }

        if(argc == 1 && (strcmp(cmd, "reg_genmove") == 0 || (ENABLE_FRISBEE_GO
            && strcmp(cmd, "frisbee-reg_genmove") == 0)))
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
            gtp_kgs_genmove_cleanup(out_fp, idn, args[0]);
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

        if(argc == 1 && ENABLE_FRISBEE_GO && (strcmp(cmd, "frisbee-accuracy") ==
            0 || strcmp(cmd, "frisbee-epsilon") == 0))
        {
            gtp_frisbee_accuracy(out_fp, idn, args[0]);
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
            if(strcmp(buf, supported_commands[i]) == 0)
            {
                command_exists = true;
                break;
            }
            else
            {
                u16 lev_dst = levenshtein_dst(supported_commands[i], buf);
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
t is wrong; please check the documentation\n", buf);
            error_msg(out_fp, idn, "syntax error");
        }
        else
        {
            if(best_dst_val < 2){
                strcpy(buf, best_dst_str);
                goto cmd_matcher;
            }
            if(best_dst_val < 4)
                fprintf(stderr, "warning: command '%s' was not understood; did \
you mean '%s'?\n", buf, best_dst_str);
            else
                fprintf(stderr, "warning: command '%s' was not understood; run \
\"help\" for a list of available commands\n", buf);

            error_msg(out_fp, idn, "unknown command");
        }
    }
}
