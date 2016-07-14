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

bool estimate_score = true;
bool save_all_games_to_file = false;
bool resign_on_timeout = false;
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
            u16 * svar = ((u16 * )tunable[i + 2]);
            *svar = atoi(value);
            return;
        }
        if(type[0] == 'f')
        {
            double * svar = ((double * )tunable[i + 2]);
            *svar = atof(value);
            return;
        }

        fprintf(stderr, "error: illegal internal parameter codification: %s\n",
            type);
        exit(EXIT_FAILURE);
    }

    fprintf(stderr,
        "error: illegal parameter name: %s\navailable parameters:\n", name);
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
    printf("\n%s\n\n", BOARD_SIZ_AS_STR);
    return 0;

    timestamp();
    bool use_gtp = false;
    bool color_set = false;
    bool human_player_color = true;
    bool think_in_opt_turn = false;
    set_logging_level(DEFAULT_LOG_LVL);
    set_time_per_turn(&current_clock_black, DEFAULT_TIME_PER_TURN);
    set_time_per_turn(&current_clock_white, DEFAULT_TIME_PER_TURN);
    s16 desired_num_threads = DEFAULT_NUM_THREADS;

    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-version") == 0)
        {
            fprintf(stderr, "matilda %u.%u\n", VERSION_MAJOR, VERSION_MINOR);
            return EXIT_SUCCESS;
        }
        if(strcmp(argv[i], "-info") == 0)
        {
            fprintf(stderr, "\n%s\n", build_info());
            return EXIT_SUCCESS;
        }
    }

    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-color") == 0 && i < argc - 1)
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
        if(strcmp(argv[i], "-gtp") == 0)
        {
            use_gtp = true;
            continue;
        }
        if(strcmp(argv[i], "-disable_score_estimation") == 0)
        {
            estimate_score = false;
            continue;
        }
        if(strcmp(argv[i], "-think_in_opt_time") == 0)
        {
            think_in_opt_turn = true;
            continue;
        }
        if(strcmp(argv[i], "-saveall") == 0)
        {
            save_all_games_to_file = true;
            continue;
        }
        if(strcmp(argv[i], "-log") == 0 && i < argc - 1)
        {
            int lvl = atoi(argv[i + 1]);
            if(lvl < LOG_NONE || lvl > LOG_INFORM)
            {
                fprintf(stderr, "error: illegal logging level\n");
                exit(EXIT_FAILURE);
            }
            set_logging_level(lvl);
            ++i;
            continue;
        }
        if(strcmp(argv[i], "-time") == 0)
        {
            if(LIMIT_BY_PLAYOUTS)
            {
                fprintf(stderr, "error: matilda has been compiled to run with \
a constant number of playouts per turn; -time flag is illegal\n");
                exit(EXIT_FAILURE);
            }
            int ftime = atoi(argv[i + 1]);
            if(ftime <= 0 || ftime >= 2147484)
            {
                fprintf(stderr, "error: illegal time format\n");
                exit(EXIT_FAILURE);
            }
            set_time_per_turn(&current_clock_black, ftime * 1000);
            set_time_per_turn(&current_clock_white, ftime * 1000);
            current_clock_black.can_timeout = false;
            current_clock_white.can_timeout = false;

            char * buf = get_buffer();
            snprintf(buf, 128, "Clock set to %s\n",
                time_system_to_str(&current_clock_black));
            fprintf(stderr, "%s", buf);
            ++i;
            continue;
        }
        if(strcmp(argv[i], "-disable_opening_books") == 0)
        {
            set_use_of_opening_book(false);
            continue;
        }
        if(strcmp(argv[i], "-resign_on_timeout") == 0)
        {
            if(LIMIT_BY_PLAYOUTS)
            {
                fprintf(stderr, "error: matilda has been compiled to run with \
a constant number of playouts per turn; -resign_on_timeout flag is illegal\n");
                exit(EXIT_FAILURE);
            }
            resign_on_timeout = true;
            continue;
        }
        if(strcmp(argv[i], "-memory") == 0 && i < argc - 1)
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
        if(strcmp(argv[i], "-set") == 0 && i < argc - 2)
        {
            set_parameter(argv[i + 1], argv[i + 2]);
            i += 2;
            continue;
        }
        if(strcmp(argv[i], "-data") == 0 && i < argc - 1)
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
        if(strcmp(argv[i], "-threads") == 0 && i < argc - 1)
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
        if(ENABLE_FRISBEE_GO && strcmp(argv[i], "-frisbee") == 0 && i < argc -
            1)
        {
            double v;
            if(!parse_float(argv[i + 1], &v))
            {
                fprintf(stderr, "error: -frisbee argument format error\n");
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

        fprintf(stderr, "\n");
        fprintf(stderr, "matilda - Go/Igo/Weiqi/Baduk computer player\n\n");
        fprintf(stderr, "Usage: matilda [options]\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr,
            "-color <color> - Select human player color (text mode only).\n");
        fprintf(stderr, "-data <path> - Override the data directory path. Must \
end in /.\n");
        fprintf(stderr, "-disable_opening_books - Disable use of opening books \
before MCTS.\n");
        fprintf(stderr, "-disable_score_estimation - Disable expensive scoring \
functions.\n");
        if(ENABLE_FRISBEE_GO)
            fprintf(stderr, "-frisbee <number> - Enable Frisbee Go with \
specified accuracy.\n");
        fprintf(stderr,
            "-gtp - Open in GTP mode. Uses the standard input and output.\n");
        fprintf(stderr, "-info - Print compile time configuration and exit.\n");
        fprintf(stderr, "-log <level> - Log messages to file. See bellow for \
logging levels.\n");
        fprintf(stderr, "-memory <number> - Set maximum size of transpositions \
table, in MiB.\n");
        fprintf(stderr,
            "-resign_on_timeout - Resign on timeout (GTP mode only).\n");
        fprintf(stderr,
            "-saveall - Save all finished games to data directory.\n");
        fprintf(stderr,
            "-set <param> <value> - Used for parameter optimization.\n");
        fprintf(stderr, "-think_in_opt_time - Use MCTS in the opponents \
turn (GTP mode only).\n");
        fprintf(stderr,
            "-threads <number> - Override the number of threads to use.\n");
        fprintf(stderr,
            "-time <seconds> - Use a fixed number of seconds per turn.\n");
        fprintf(stderr, "-version - Print version information and exit.\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Logging levels:\n");
        fprintf(stderr, "0 - No logging.\n");
        fprintf(stderr, "1 - Critical errors. (default)\n");
        fprintf(stderr, "2 - Warnings and protocol communication.\n");
        fprintf(stderr, "3 - Informational messages.\n");
        fprintf(stderr, "\n");
        return EXIT_FAILURE;
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

