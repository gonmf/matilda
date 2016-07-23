/*
Entry point for Matilda -- parses the program flags and starts the program in
either GTP or text mode.

Also deals with updating some internal parameters at startup time.
*/

#include "matilda.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <omp.h>

#include "board.h"
#include "cfg_board.h"
#include "engine.h"
#include "flog.h"
#include "game_record.h"
#include "opening_book.h"
#include "randg.h"
#include "stringm.h"
#include "timem.h"
#include "time_ctrl.h"
#include "zobrist.h"
#include "buffer.h"

game_record current_game;
time_system current_clock_black;
time_system current_clock_white;

bool estimate_score = true; /* perform slow score estimating */
bool time_system_overriden = false; /* ignore attempts to change time system */
bool save_all_games_to_file = false; /* save all games as SGF on gameover */
bool resign_on_timeout = false; /* resign instead of passing if timed out */

extern u64 max_size_in_mbs;
extern float frisbee_prob;

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
extern u16 prior_line1;
extern u16 prior_line2;
extern u16 prior_line3;
extern u16 prior_empty;
extern u16 prior_line1x;
extern u16 prior_line2x;
extern u16 prior_line3x;
extern u16 prior_corner;
extern double ucb1_c;
extern double rave_mse_b;
extern u16 pl_skip_saving;
extern u16 pl_skip_nakade;
extern u16 pl_skip_pattern;
extern u16 pl_skip_capture;
static u16 _dummy; /* used for testing CLOP */



static u16 avg_game_length; // TODO to remove after paper, E


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
    "i", "prior_line1", &prior_line1,
    "i", "prior_line2", &prior_line2,
    "i", "prior_line3", &prior_line3,
    "i", "prior_empty", &prior_empty,
    "i", "prior_line1x", &prior_line1x,
    "i", "prior_line2x", &prior_line2x,
    "i", "prior_line3x", &prior_line3x,
    "i", "prior_corner", &prior_corner,
    "f", "ucb1_c", &ucb1_c,
    "f", "rave_mse_b", &rave_mse_b,
    "i", "pl_skip_saving", &pl_skip_saving,
    "i", "pl_skip_nakade", &pl_skip_nakade,
    "i", "pl_skip_pattern", &pl_skip_pattern,
    "i", "pl_skip_capture", &pl_skip_capture,
    "i", "dummy", &_dummy,


    "i", "avg_game_length", &avg_game_length, // TODO remove


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
            s32 val;
            if(!parse_int(value, &val) || val < 0)
            {
                fprintf(stderr, "format error: %s\n", value);
                exit(EXIT_FAILURE);
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
                fprintf(stderr, "format error: %s\n", value);
                exit(EXIT_FAILURE);
            }
            double * svar = ((double * )tunable[i + 2]);
            *svar = val;
            return;
        }

        fprintf(stderr, "error: illegal internal parameter codification: %s\n",
            type);
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "error: illegal parameter name: %s\n", name);
    fprintf(stderr, "available parameters:\n");

    for(u16 i = 0; tunable[i] != NULL; i += 3)
    {
        char * type = ((char * )tunable[i]);
        char * sname = ((char * )tunable[i + 1]);
        if(type[0] == 'i')
            fprintf(stderr, "(%s) %s: %u\n", type, sname, *((u16 * )tunable[i +
                2]));
        else
            fprintf(stderr, "(%s) %s: %.2f\n", type, sname, *((double *
                )tunable[i + 2]));
    }
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
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
    timestamp();
    bool use_gtp = (isatty(STDIN_FILENO) == 0);
    bool color_set = false;
    bool human_player_color = true;
    bool think_in_opt_turn = false;
    set_logging_level(DEFAULT_LOG_LVL);
    set_time_per_turn(&current_clock_black, DEFAULT_TIME_PER_TURN);
    set_time_per_turn(&current_clock_white, DEFAULT_TIME_PER_TURN);
    bool time_changed_or_set = false;
    s16 desired_num_threads = DEFAULT_NUM_THREADS;

    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr, "matilda %u.%u\n", VERSION_MAJOR, VERSION_MINOR);
            return EXIT_SUCCESS;
        }
        if(strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--info") == 0)
        {
            fprintf(stderr, "\n%s\n", build_info());
            return EXIT_SUCCESS;
        }
    }

    for(int i = 1; i < argc; ++i)
    {
        if((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) && i
            < argc - 1)
        {
            if(strcmp(argv[i + 1], "text") == 0)
            {
                use_gtp = false;
            }
            else
                if(strcmp(argv[i + 1], "gtp") == 0)
                {
                    use_gtp = true;
                }
                else
                {
                    fprintf(stderr, "error: illegal format for mode\n");
                    exit(EXIT_FAILURE);
                }
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
                {
                    fprintf(stderr,
                        "error: illegal format when specifying player color\n");
                    exit(EXIT_FAILURE);
                }
            ++i;
            color_set = true;
            continue;
        }
        if(strcmp(argv[i], "--disable_score_estimation") == 0)
        {
            estimate_score = false;
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
        if((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--log") == 0) && i <
            argc - 1)
        {
            s32 lvl;
            if(!parse_int(argv[i + 1], &lvl) || lvl < LOG_NONE || lvl >
                LOG_INFORM)
            {
                fprintf(stderr, "error: illegal logging level\n");
                exit(EXIT_FAILURE);
            }
            set_logging_level(lvl);
            ++i;
            continue;
        }
        if(strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--time") == 0)
        {
            if(LIMIT_BY_PLAYOUTS)
            {
                fprintf(stderr, "error: matilda has been compiled to run with a\
 constant number of playouts per turn; --time flag is illegal\n");
                exit(EXIT_FAILURE);
            }
            int ftime;
            if(!parse_int(argv[i + 1], &ftime) || ftime <= 0 || ftime >=
                2147484)
            {
                fprintf(stderr, "error: illegal time format\n");
                exit(EXIT_FAILURE);
            }

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
            {
                fprintf(stderr, "error: matilda has been compiled to run with a\
 constant number of playouts per turn; --time_system flag is illegal\n");
                exit(EXIT_FAILURE);
            }

            time_system tmp;
            if(!str_to_time_system(argv[i + 1], &tmp))
            {
                fprintf(stderr, "error: illegal time system string format\n");
                flog_crit("error: illegal time system string format\n");
                exit(EXIT_FAILURE);
            }

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
            {
                fprintf(stderr, "error: matilda has been compiled to run with a\
 constant number of playouts per turn; --resign_on_timeout flag is illegal\n");
                exit(EXIT_FAILURE);
            }
            resign_on_timeout = true;
            continue;
        }
        if(strcmp(argv[i], "--memory") == 0 && i < argc - 1)
        {
            s32 v;
            if(!parse_int(argv[i + 1], &v))
            {
                fprintf(stderr,
                    "error: format error in size of transpositions table\n");
                exit(EXIT_FAILURE);
            }
            if(v < 2)
            {
                fprintf(stderr,
                    "error: invalid size for transpositions table\n");
                exit(EXIT_FAILURE);
            }
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
                fprintf(stderr, "error: data directory path %s is not valid\n",
                    argv[i + 1]);
                exit(EXIT_FAILURE);
            }

            ++i;
            continue;
        }
        if(strcmp(argv[i], "--threads") == 0 && i < argc - 1)
        {
            s32 v;
            if(!parse_int(argv[i + 1], &v))
            {
                fprintf(stderr, "error: -threads argument format error\n");
                exit(EXIT_FAILURE);
            }
            if(v < 1 || v >= MAXIMUM_NUM_THREADS)
            {
                fprintf(stderr, "error: invalid number of threads requested\n");
                exit(EXIT_FAILURE);
            }
            desired_num_threads = v;
            ++i;
            continue;
        }
        if(strcmp(argv[i], "--frisbee_accuracy") == 0 && i < argc - 1)
        {
            if(!ENABLE_FRISBEE_GO)
            {
                fprintf(stderr, "error: program must be compiled with support f\
or frisbee play\n");
                flog_crit("error: program must be compiled with support for fri\
sbee play\n");
                exit(EXIT_FAILURE);
            }

            double v;
            if(!parse_float(argv[i + 1], &v))
            {
                fprintf(stderr, "error: accuracy argument format error\n");
                exit(EXIT_FAILURE);
            }
            if(v < 0.0 || v > 1.0)
            {
                fprintf(stderr, "error: invalid frisbee accuracy\n");
                exit(EXIT_FAILURE);
            }
            frisbee_prob = v;
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

        fprintf(stderr,
            "        \033[1m-c, --color <black or white>\033[0m\n\n");
        fprintf(stderr,
            "        Select human player color (text mode only).\n\n");

        fprintf(stderr, "        \033[1m--resign_on_timeout\033[0m\n\n");
        fprintf(stderr,
            "        Resign if the program believes to have lost on time.\n\n");

        fprintf(stderr, "        \033[1m--think_in_opt_time\033[0m\n\n");
        fprintf(stderr, "        Continue thinking in the background while in \
the opponents turn.\n\n");

        fprintf(stderr, "        \033[1m-t, --time <number>\033[0m\n\n");
        fprintf(stderr, "        Set the time system to a specific number of \
seconds per turn and ignore timeouts.\n\n");

        fprintf(stderr, "        \033[1m--time_system <value>\033[0m\n\n");
        fprintf(stderr, "        Override the time system in use and ignore \
changes via GTP.\n        Use a byoyomi format like 10m+3x30s/5; allowed \
specifiers: ms, s, m, h.\n\n");

        fprintf(stderr, "        \033[1m-d, --data <path>\033[0m\n\n");
        fprintf(stderr, "        Override the data folder path. The folder \
must exist.\n\n");

        fprintf(stderr, "        \033[1m--disable_opening_books\033[0m\n\n");
        fprintf(stderr, "        Disable the use of opening books.\n\n");

        fprintf(stderr, "        \033[1m--disable_score_estimation\033[0m\n\n");
        fprintf(stderr, "        Disable final scoring estimates.\n\n");

        fprintf(stderr, "        \033[1m-l, --log <level>\033[0m\n\n");
        fprintf(stderr, "        Set the message logger level. The available \
levels are:\n         0 - No logging\n         1 - Critical error messages \
(default)\n         2 - Errors, warning and GTP trace\n         3 - All \
messages\n\n");

        fprintf(stderr, "        \033[1m--memory <number>\033[0m\n\n");
        fprintf(stderr, "        Override the available memory for the MCTS \
transpositions table, in MiB.\n        The default is %u MiB\n\n",
            DEFAULT_UCT_MEMORY);

        fprintf(stderr, "        \033[1m--save_all\033[0m\n\n");
        fprintf(stderr,
            "        Save all finished games to the data folder as SGF.\n\n");

        fprintf(stderr, "        \033[1m--frisbee_accuracy <number>\033[0m\n\n");
        fprintf(stderr,
            "        Select frisbee accuracy if playing frisbee Go.\n\n");

        fprintf(stderr, "        \033[1m--threads <number>\033[0m\n\n");
        fprintf(stderr, "        Override the number of OpenMP threads to use. \
The default is the total\n        number of normal plus hyperthreaded CPU \
cores.\n\n");

        fprintf(stderr, "        \033[1m--set <param> <value>\033[0m\n\n");
        fprintf(stderr, "        For optimization. Set the value of an \
internal parameter.\n\n");

        fprintf(stderr, "        \033[1m-i, --info\033[0m\n\n");
        fprintf(stderr,
            "        Print runtime information at startup and exit.\n\n");

        fprintf(stderr, "        \033[1m-v, --version\033[0m\n\n");
        fprintf(stderr, "        Print version information and exit.\n\n");

        fprintf(stderr, "\033[1mBUGS\033[0m\n");
        fprintf(stderr, "        You can provide feedback at \
https://github.com/gonmf/matilda\n\n");

        return EXIT_FAILURE;
    }

    if(time_changed_or_set)
    {
        char * buf = get_buffer();
        snprintf(buf, 128, "Clock set to %s\n",
            time_system_to_str(&current_clock_black));
        fprintf(stderr, "%s", buf);
    }

    /*
    Errors for runtime options
    */
    if(think_in_opt_turn)
    {
        if(LIMIT_BY_PLAYOUTS)
        {
            fprintf(stderr, "error: -think_in_opt_time flag cannot be used \
with the program compiled to use a constant number of playouts per turn\n");
            flog_crit("error: -think_in_opt_time flag cannot be used with the \
program compiled to use a constant number of playouts per turn\n");
            return EXIT_FAILURE;
        }

        if(!use_gtp)
        {
            fprintf(stderr,
                "error: -think_in_opt_time flag set outside of GTP mode\n");
            flog_crit(
                "error: -think_in_opt_time flag set outside of GTP mode\n");
            return EXIT_FAILURE;
        }
    }

    if(use_gtp && color_set)
    {
        fprintf(stderr, "error: -color flag set in GTP mode\n");
        flog_crit("error: -color flag set in GTP mode\n");
        return EXIT_FAILURE;
    }


    if(!use_gtp)
        fclose(stderr);


    /*
    Warnings for compile time options
    */

#if !MATILDA_RELEASE_MODE
    fprintf(stderr, "warning: running on debug mode\n");
    flog_warn("warning: running on debug mode\n");
#endif

#if LIMIT_BY_PLAYOUTS
    fprintf(stderr,
        "warning: MCTS will use a constant number of simulations per turn\n");
    flog_warn(
        "warning: MCTS will use a constant number of simulations per turn\n");
#endif


    assert_data_folder_exists();
    rand_init();
    cfg_board_init();
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

