/*
Entry point for Matilda -- parses the program flags and starts the program in
either GTP or text mode.

Also deals with updating some internal parameters at startup time.
*/

#include "matilda.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <omp.h>

#include "alloc.h"
#include "board.h"
#include "cfg_board.h"
#include "constants.h"
#include "engine.h"
#include "flog.h"
#include "game_record.h"
#include "opening_book.h"
#include "randg.h"
#include "stringm.h"
#include "time_ctrl.h"
#include "timem.h"
#include "zobrist.h"
#include "version.h"

game_record current_game;
time_system current_clock_black;
time_system current_clock_white;

bool time_system_overriden = false; /* ignore attempts to change time system */
bool save_all_games_to_file = false; /* save all games as SGF on gameover */
bool resign_on_timeout = false; /* resign instead of passing if timed out */

extern u64 max_size_in_mbs;

/*
For tuning
*/
extern double prior_stone_scale_factor;
extern u16 prior_even;
extern u16 prior_nakade;
extern u16 prior_self_atari;
extern u16 prior_attack;
extern u16 prior_defend;
extern u16 prior_pat3;
extern u16 prior_near_last;
extern u16 prior_line2;
extern u16 prior_line3;
extern u16 prior_empty;
extern u16 prior_line1x;
extern u16 prior_line2x;
extern u16 prior_line3x;
extern u16 prior_corner;
extern u16 prior_bad_play;
extern u16 prior_pass;
extern double rave_equiv;
extern u16 pl_skip_saving;
extern u16 pl_skip_nakade;
extern u16 pl_skip_pattern;
extern u16 pl_skip_capture;
extern u16 expansion_delay;
static u16 _dummy; /* used for testing CLOP */



static double time_allot_factor; // TODO to remove after paper, E


const void * tunable[] =
{
    "f", "prior_stone_scale_factor", &prior_stone_scale_factor,
    "i", "prior_even", &prior_even,
    "i", "prior_nakade", &prior_nakade,
    "i", "prior_self_atari", &prior_self_atari,
    "i", "prior_attack", &prior_attack,
    "i", "prior_defend", &prior_defend,
    "i", "prior_pat3", &prior_pat3,
    "i", "prior_near_last", &prior_near_last,
    "i", "prior_line2", &prior_line2,
    "i", "prior_line3", &prior_line3,
    "i", "prior_empty", &prior_empty,
    "i", "prior_line1x", &prior_line1x,
    "i", "prior_line2x", &prior_line2x,
    "i", "prior_line3x", &prior_line3x,
    "i", "prior_corner", &prior_corner,
    "i", "prior_bad_play", &prior_bad_play,
    "i", "prior_pass", &prior_pass,
    "f", "rave_equiv", &rave_equiv,
    "i", "pl_skip_saving", &pl_skip_saving,
    "i", "pl_skip_nakade", &pl_skip_nakade,
    "i", "pl_skip_pattern", &pl_skip_pattern,
    "i", "pl_skip_capture", &pl_skip_capture,
    "i", "expansion_delay", &expansion_delay,
    "i", "dummy", &_dummy,


    "f", "time_allot_factor", &time_allot_factor, // TODO remove


    NULL
};

static void set_parameter(
    const char * name,
    const char * value
){
    for(u16 i = 0; tunable[i] != NULL; i += 3)
    {
        char * sname = ((char * )tunable[i + 1]);

        if(strcmp(sname, name) != 0)
            continue;

        char * type = ((char * )tunable[i]);

        if(type[0] == 'i')
        {
            d32 val;
            if(!parse_int(value, &val) || val < 0)
            {
                char * buf = alloc();
                snprintf(buf, MAX_PAGE_SIZ, "integer format error: %s", value);
                flog_crit("main", buf);
                release(buf);
            }

            u16 * svar = ((u16 * )tunable[i + 2]);
            *svar = val;
            return;
        }
        if(type[0] == 'f')
        {
            double val;
            if(!parse_float(value, &val))
            {
                char * buf = alloc();
                snprintf(buf, MAX_PAGE_SIZ, "float format error: %s", value);
                flog_crit("main", buf);
                release(buf);
            }

            double * svar = ((double * )tunable[i + 2]);
            *svar = val;
            return;
        }

        char * buf = alloc();
        snprintf(buf, MAX_PAGE_SIZ,
            "illegal internal parameter codification: %s", type);
        flog_crit("main", buf);
        release(buf);
    }

    char * buf = alloc();
    snprintf(buf, MAX_PAGE_SIZ, "illegal parameter name: %s", name);
    flog_crit("main", buf);
    release(buf);
}

void main_gtp(
    bool think_in_opt_turn
);
void main_text(
    bool is_black
);

int main(
    int argc,
    char * argv[]
){
    alloc_init();
    config_logging(DEFAULT_LOG_MODES);
    bool use_gtp = (isatty(STDIN_FILENO) == 0);
    bool color_set = false;
    bool human_player_color = true;
    bool think_in_opt_turn = false;
    set_time_per_turn(&current_clock_black, DEFAULT_TIME_PER_TURN);
    set_time_per_turn(&current_clock_white, DEFAULT_TIME_PER_TURN);
    bool time_changed_or_set = false;
    d16 desired_num_threads = DEFAULT_NUM_THREADS;

    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr, "matilda %s\n", MATILDA_VERSION);
            return EXIT_SUCCESS;
        }
        if(strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--info") == 0)
        {
            char * s = alloc();
            build_info(s);
            fprintf(stderr, "\n%s\n", s);
            release(s);
            return EXIT_SUCCESS;
        }
    }

    for(int i = 1; i < argc; ++i)
    {
        if((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) && i
            < argc - 1)
        {
            if(strcmp(argv[i + 1], "text") == 0)
                use_gtp = false;
            else
                if(strcmp(argv[i + 1], "gtp") == 0)
                    use_gtp = true;
                else
                    flog_crit("main", "illegal format for mode");

            ++i;
            continue;
        }
        if((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--color") == 0) && i
            < argc - 1)
        {
            if(argv[i + 1][0] == 'b' || argv[i + 1][0] == 'B')
                human_player_color = true;
            else
                if(argv[i + 1][0] == 'w' || argv[i + 1][0] == 'W')
                    human_player_color = false;
                else
                    flog_crit("main", "illegal format when specifying player c\
olor");

            ++i;
            color_set = true;
            continue;
        }
        if(strcmp(argv[i], "--think_in_opt_time") == 0)
        {
            think_in_opt_turn = true;
            continue;
        }
        if(strcmp(argv[i], "--save_all") == 0)
        {
            save_all_games_to_file = true;
            continue;
        }
        if((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--log") == 0))
        {
            if(i == argc - 1)
            {
                config_logging(0);
                continue;
            }

            if(argv[i + 1][0] == '-')
            {
                config_logging(0);
                continue;
            }

            u16 mode = 0;
            for(u16 j = 0; argv[i + 1][j]; ++j)
            {
                if(argv[i + 1][j] == 'e')
                {
                    mode |= LOG_CRITICAL;
                    continue;
                }
                if(argv[i + 1][j] == 'w')
                {
                    mode |= LOG_WARNING;
                    continue;
                }
                if(argv[i + 1][j] == 'p')
                {
                    mode |= LOG_PROTOCOL;
                    continue;
                }
                if(argv[i + 1][j] == 'i')
                {
                    mode |= LOG_INFORMATION;
                    continue;
                }
                if(argv[i + 1][j] == 'd')
                {
                    mode |= LOG_DEBUG;
                    continue;
                }

                char * buf = alloc();
                snprintf(buf, MAX_PAGE_SIZ, "illegal logging mode: %c", argv[i +
                    1][j]);
                flog_crit("main", buf);
                release(buf);
            }
            config_logging(mode);

            ++i;
            continue;
        }
        if(strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--time") == 0)
        {
            if(LIMIT_BY_PLAYOUTS)
                flog_crit("main", "matilda has been compiled to run with a con\
stant number of playouts per turn; --time flag is illegal");

            int ftime;
            if(!parse_int(argv[i + 1], &ftime) || ftime <= 0 || ftime >=
                2147484)
                flog_crit("main", "illegal time format");

            if(time_system_overriden)
            {
                ++i;
                continue;
            }

            set_time_per_turn(&current_clock_black, ftime * 1000);
            set_time_per_turn(&current_clock_white, ftime * 1000);
            current_clock_black.can_timeout = false;
            current_clock_white.can_timeout = false;

            time_changed_or_set = true;

            ++i;
            continue;
        }
        if(strcmp(argv[i], "--time_system") == 0)
        {
            if(LIMIT_BY_PLAYOUTS)
                flog_crit("main", "matilda has been compiled to run with a con\
stant number of playouts per turn; --time_system flag is illegal");

            time_system tmp;
            if(!str_to_time_system(&tmp, argv[i + 1]))
                flog_crit("main", "illegal time system string format");

            set_time_system(&current_clock_black, tmp.main_time,
                tmp.byo_yomi_time, tmp.byo_yomi_stones, tmp.byo_yomi_periods);
            set_time_system(&current_clock_white, tmp.main_time,
                tmp.byo_yomi_time, tmp.byo_yomi_stones, tmp.byo_yomi_periods);

            time_system_overriden = true;
            time_changed_or_set = true;

            ++i;
            continue;
        }
        if(strcmp(argv[i], "--disable_opening_books") == 0)
        {
            set_use_of_opening_book(false);
            continue;
        }
        if(strcmp(argv[i], "--resign_on_timeout") == 0)
        {
            if(LIMIT_BY_PLAYOUTS)
                flog_crit("main", "matilda has been compiled to run with a con\
stant number of playouts per turn; --resign_on_timeout flag is illegal");

            resign_on_timeout = true;
            continue;
        }
        if(strcmp(argv[i], "--memory") == 0 && i < argc - 1)
        {
            d32 v;
            if(!parse_int(argv[i + 1], &v))
                flog_crit("main", "error: format error in size of transpositio\
ns table");

            if(v < 2)
                flog_crit("main", "error: invalid size for transpositions tabl\
e");

            max_size_in_mbs = v;
            ++i;
            continue;
        }
        if(strcmp(argv[i], "--set") == 0 && i < argc - 2)
        {
            set_parameter(argv[i + 1], argv[i + 2]);
            i += 2;
            continue;
        }
        if((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--data") == 0) && i <
            argc - 1)
        {
            if(!set_data_folder(argv[i + 1]))
            {
                char * buf = alloc();
                snprintf(buf, MAX_PAGE_SIZ, "data directory path %s is not vali\
d", argv[i + 1]);
                flog_crit("main", buf);
                release(buf);
            }

            ++i;
            continue;
        }
        if(strcmp(argv[i], "--threads") == 0 && i < argc - 1)
        {
            d32 v;
            if(!parse_int(argv[i + 1], &v))
                flog_crit("main", "--threads argument format error");

            if(v < 1 || v > MAXIMUM_NUM_THREADS)
                flog_crit("main", "invalid number of threads requested");

            desired_num_threads = v;
            ++i;
            continue;
        }

        fprintf(stderr, "matilda - Go/Igo/Weiqi/Baduk computer player\n\n");

        fprintf(stderr, "\033[1mUSAGE\033[0m\n");
        fprintf(stderr, "        matilda [options]\n\n");
        fprintf(stderr, "\033[1mDESCRIPTION\033[0m\n");
        fprintf(stderr, "        Matilda is a computer program that plays the g\
ame of Go. It uses Chinese\n        rules without life in seki.\n        Two in\
terface modes are available: a simple text interface, and the Go\n        Text \
Protocol through the standard input and output file descriptors.\n        Most \
more advanced features, like file manipulation and game analysis,\n        are \
only available through GTP commands. To learn more about them\n        consult \
the file GTP_README.\n        All files read and written, including SGF, reside\
 in the data folder.\n\n");
        fprintf(stderr, "\033[1mOPTIONS\033[0m\n");

        fprintf(stderr, "        \033[1m-m, --mode <gtp or text>\033[0m\n\n");
        fprintf(stderr, "        Matilda attempts to detect if its input file d\
escriptor is a terminal\n        and if it is it uses the text mode interface. \
Otherwise it uses the GTP\n        interface. This command overrides this with \
the specific mode you want\n        to be used.\n\n");

        fprintf(stderr, "        \033[1m-c, --color <black or white>\033[0m\n\n\
");
        fprintf(stderr, "        Select human player color (text mode only).\n\\
n");

        fprintf(stderr, "        \033[1m--resign_on_timeout\033[0m\n\n");
        fprintf(stderr, "        Resign if the program believes to have lost on\
 time.\n\n");

        fprintf(stderr, "        \033[1m--think_in_opt_time\033[0m\n\n");
        fprintf(stderr, "        Continue thinking in the background while in t\
he opponents turn.\n\n");

        fprintf(stderr, "        \033[1m-t, --time <number>\033[0m\n\n");
        fprintf(stderr, "        Set the time system to a specific number of se\
conds per turn and ignore\n        timeouts.\n\n");

        fprintf(stderr, "        \033[1m--time_system <value>\033[0m\n\n");
        fprintf(stderr, "        Override the time system in use and ignore cha\
nges via GTP.\n        Use a byoyomi format like 10m+3x30s/5; allowed specifier\
s: ms, s, m, h.\n\n");

        fprintf(stderr, "        \033[1m-d, --data <path>\033[0m\n\n");
        fprintf(stderr, "        Override the data folder path. The folder must\
 exist.\n\n");

        fprintf(stderr, "        \033[1m--disable_opening_books\033[0m\n\n");
        fprintf(stderr, "        Disable the use of opening books.\n\n");

        fprintf(stderr, "        \033[1m-l, --log <modes>\033[0m\n\n");
        fprintf(stderr, "        Set the message types to log to file and print\
 to the standard error\n        file descriptor. The available modes are:\n\n  \
        e - Error messages\n          w - Warning messages\n          p - Proto\
col messages\n          i - Informational messages\n          d - Debugging mes\
sages\n\n        Default setting: --log ew\n        Leave empty for no logging.\
 Notice log printing to the standard error\n        file descriptor may be mute\
d in text mode.\n\n");

        fprintf(stderr, "        \033[1m--memory <number>\033[0m\n\n");
        fprintf(stderr, "        Override the available memory for the MCTS tra\
nspositions table, in MiB.\n        The default is %u MiB\n\n",
            DEFAULT_UCT_MEMORY);

        fprintf(stderr, "        \033[1m--save_all\033[0m\n\n");
        fprintf(stderr, "        Save all finished games to the data folder as \
SGF.\n\n");

        fprintf(stderr, "        \033[1m--threads <number>\033[0m\n\n");
        fprintf(stderr, "        Override the number of OpenMP threads to use. \
The default is the total\n        number of normal plus hyperthreaded CPU cores\
.\n\n");

        fprintf(stderr, "        \033[1m--set <param> <value>\033[0m\n\n");
        fprintf(stderr, "        For optimization. Set the value of an internal\
 parameter.\n\n");

        fprintf(stderr, "        \033[1m-i, --info\033[0m\n\n");
        fprintf(stderr, "        Print runtime information at startup and exit.\
\n\n");

        fprintf(stderr, "        \033[1m-v, --version\033[0m\n\n");
        fprintf(stderr, "        Print version information and exit.\n\n");

        fprintf(stderr, "\033[1mBUGS\033[0m\n");
        fprintf(stderr, "        You can provide feedback at https://github.com\
/gonmf/matilda\n\n");

        flog_crit("main", "unknown parameter");
        return EXIT_FAILURE;
    }

    if(time_changed_or_set)
    {
        char * s1 = alloc();
        char * s2 = alloc();
        time_system_to_str(s1, &current_clock_black);
        snprintf(s2, MAX_PAGE_SIZ, "Clock set to %s\n", s1);
        fprintf(stderr, "%s", s2);
        release(s2);
        release(s1);
    }

    /*
    Errors for runtime options
    */
    if(think_in_opt_turn)
    {
        if(LIMIT_BY_PLAYOUTS)
            flog_crit("main", "--think_in_opt_time flag cannot be used with th\
e program compiled to use a constant number of playouts per turn");

        if(!use_gtp)
            flog_crit("main", "--think_in_opt_time flag set outside of GTP mod\
e");
    }

    if(use_gtp && color_set)
        flog_crit("main", "--color flag set in GTP mode");

    if(!use_gtp)
        fclose(stderr);


    /*
    Warnings for compile time options
    */

#if !MATILDA_RELEASE_MODE
    flog_warn("main", "running on debug mode");
#endif

#if LIMIT_BY_PLAYOUTS
    flog_warn("main",
        "MCTS will use a constant number of simulations per turn");
#endif


    assert_data_folder_exists();
    rand_init();
    board_constants_init();
    zobrist_init();

    u32 automatic_num_threads;
    #pragma omp parallel
    #pragma omp master
    {
        automatic_num_threads = omp_get_num_threads();
    }
    if(automatic_num_threads > MAXIMUM_NUM_THREADS)
        omp_set_num_threads(MAXIMUM_NUM_THREADS);
    if(desired_num_threads > 0)
        omp_set_num_threads(MIN(desired_num_threads, MAXIMUM_NUM_THREADS));
    omp_set_dynamic(0);

    if(use_gtp)
        main_gtp(think_in_opt_turn);
    else
        main_text(human_player_color);

    return EXIT_SUCCESS;
}

