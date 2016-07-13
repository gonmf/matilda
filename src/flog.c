/*
Support for logging to file. Logging is made to a file called matilda-XXXXXX.log
where XXXXXX is a random string. When logging a logging level is set which
specifies the degree of detail of the messages to be written to file. Having a
very high degree of detail in very fast matches actively hurts the performance.

Writing to files is synchronous (with fsync) to avoid loss of data in case of
crashes, but it is impossible to guarantee this in all cases.
*/

#include "matilda.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "amaf_rave.h"
#include "engine.h"
#include "flog.h"
#include "mcts.h"
#include "pat3.h"
#include "playout.h"
#include "scoring.h"
#include "time_ctrl.h"
#include "types.h"
#include "buffer.h"

static int log_file = -1;
static u8 log_lvl = LOG_NONE;
static char log_filename[32];



/*
For non-default values for build_info
*/
extern u64 max_size_in_mbs;
extern float frisbee_prob;
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
extern u16 prior_corner;
extern double ucb1_c;
extern double rave_mse_b;
extern u16 pl_skip_saving;
extern u16 pl_skip_nakade;
extern u16 pl_skip_pattern;
extern u16 pl_skip_capture;
extern s16 komi;



/*
Set logging level of messages to actually write to file.
Messages more verbose than the level specified are ignored.
*/
void set_logging_level(
    u8 lvl
){
    if(lvl != LOG_NONE && log_file != -1)
    {
        char * buf = get_buffer();
        snprintf(buf, MAX_PAGE_SIZ, "\n--- log detail level changed to %u\n\n",
            lvl);
        u32 len = strlen(buf);
        write(log_file, buf, len);
        fsync(log_file);
    }

    if(lvl == LOG_NONE)
    {
        if(log_file != -1)
            close(log_file);
        log_file = -1;
    }

    log_lvl = lvl;
}

static void open_log_file()
{
    if(log_file == -1)
    {
        snprintf(log_filename, 32, "matilda-XXXXXX.log");
        log_file = mkstemps(log_filename, 4);
        if(log_file == -1)
        {
            fprintf(stderr, "Failed to open log file '%s'.\n", log_filename);
            exit(EXIT_FAILURE);
        }

        char * buf = get_buffer();
        snprintf(buf, MAX_PAGE_SIZ,
            "\n--- matilda %u.%u log started (detail level %u)\n\n",
            VERSION_MAJOR, VERSION_MINOR, log_lvl);
        u32 len = strlen(buf);
        write(log_file, buf, len);
        fsync(log_file);
    }
}


/*
Obtain a textual description of the capabilities and configuration options of
matilda. This mostly concerns compile time constants.
RETURNS string with build information
*/
const char * build_info()
{
    char * buf = get_buffer();
    char * tmp = buf;
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "Matilda build information\n");
    if(MATILDA_RELEASE_MODE)
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "Compiled for: release\n");
    else
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "Compiled for: debugging\n");
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp, "Version: %u.%u\n",
        VERSION_MAJOR, VERSION_MINOR);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp, "Data folder: %s\n",
        get_data_folder());
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "Frisbee Go enabled: %s\n", YN(ENABLE_FRISBEE_GO));

    if(ENABLE_FRISBEE_GO)
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "  Accuracy: %.2f\n", frisbee_prob);

    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp, "Board size: %ux%u\n",
        BOARD_SIZ, BOARD_SIZ);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "Komidashi: %s stones\n", komi_to_string(komi));
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "MCTS-UCT branch limiter: %s\n", YN(USE_UCT_BRANCH_LIMITER));
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp, "Can resign: %s\n",
        YN(CAN_RESIGN));
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp, "Can stop early: %s\n",
        YN(CAN_STOP_EARLY));
    if(CAN_STOP_EARLY)
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "  Min/max win rate: %.2f/%.2f\n", UCT_MIN_WINRATE,
            UCT_MAX_WINRATE);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "Transpositions table memory: %lu MiB\n", max_size_in_mbs);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "Limit by playouts instead of time: %s\n", YN(LIMIT_BY_PLAYOUTS));

    if(LIMIT_BY_PLAYOUTS)
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "  Playouts per turn: %u\n", PLAYOUTS_PER_TURN);

    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "  Chance of skipping save: 1:%u\n", pl_skip_saving);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "  Chance of skipping capture: 1:%u\n", pl_skip_capture);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "  Chance of skipping pattern: 1:%u\n", pl_skip_pattern);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "  Chance of skipping nakade: 1:%u\n", pl_skip_nakade);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "  Use pattern weights: %s\n", YN(USE_PATTERN_WEIGHTS));
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "Use AMAF/RAVE: %s\n", YN(USE_AMAF_RAVE));
    if(USE_AMAF_RAVE)
    {
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "  MSE b constant: %.2f\n", rave_mse_b);
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "  Criticality threshold: %u\n", CRITICALITY_THRESHOLD);
    }
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "UCB1-TUNED coefficient: %.2f\n", ucb1_c);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "  Stone value scale factor: %.1f\n", prior_stone_scale_factor);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp, "  Even: %u\n",
        prior_even);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp, "  Nakade: %u\n",
        prior_nakade);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp, "  Self-atari: -%u\n",
        prior_self_atari);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "  Attack 1/2 lib group: %u\n", prior_attack);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "  Defend 1/2 lib group: %u\n", prior_defend);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "  MoGo patterns: %u\n", prior_pat3);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "  Near last play: %u\n", prior_near_last);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "  Empty L1/2/3/other: -%u/-%u/%u/%u\n", prior_line1, prior_line2,
        prior_line3, prior_empty);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp, "  Corners: -%u\n",
        prior_corner);

    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp, "Max UCT depth: %u\n",
        MAX_UCT_DEPTH);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "UCT expansion delay: %u\n", UCT_EXPANSION_DELAY);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "Playout depth over number of empty points: %u\n",
        MAX_PLAYOUT_DEPTH_OVER_EMPTY);
    if(UCT_MIN_WINRATE <= 0.0)
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "UCT winrate for resigning: disabled\n");
    else
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "UCT winrate for resigning: %.2f%%\n", UCT_MIN_WINRATE);
    if(UCT_MAX_WINRATE >= 1.0)
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "UCT winrate for passing: disabled\n");
    else
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "UCT winrate for passing: %.2f%%\n", UCT_MAX_WINRATE);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "Mercy threshold: %u stones\n", MERCY_THRESHOLD);

    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "Detect network latency: %s\n", YN(DETECT_NETWORK_LATENCY));
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "Constant latency compensation: %u ms\n", LATENCY_COMPENSATION);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "Time allotment factor: %.2f\n", TIME_ALLOT_FACTOR);

    u32 num_threads;
    #pragma omp parallel
    #pragma omp master
    {
        num_threads = omp_get_num_threads();
    }
    if(DEFAULT_NUM_THREADS == 0)
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "Default number of threads: automatic (%u)\n", num_threads);
    else
        tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
            "Default number of threads: %u (%u)\n", DEFAULT_NUM_THREADS,
            num_threads);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp,
        "Maximum number of threads: %u\n", MAXIMUM_NUM_THREADS);
    tmp += snprintf(tmp, MAX_PAGE_SIZ + buf - tmp, "\n");

    return buf;
}

/*
Logs the build information to file (level: informational).
*/
void flog_build_info()
{
    flog_info(build_info());
}

/*
Log a message with verbosity level critical.
*/
void flog_crit(
    const char * s
){
    if(log_lvl < LOG_CRITICAL)
        return;

    open_log_file();
    u32 len = strlen(s);
    write(log_file, s, len);
    fsync(log_file);
}


/*
Log a message with verbosity level warning.
*/
void flog_warn(
    const char * s
){
    if(log_lvl < LOG_GTP_WARN)
        return;

    open_log_file();
    u32 len = strlen(s);
    write(log_file, s, len);
    fsync(log_file);
}


/*
Log a message with verbosity level protocolar.
*/
void flog_prot(
    const char * s
){
    if(log_lvl < LOG_GTP_WARN)
        return;

    open_log_file();
    u32 len = strlen(s);
    write(log_file, s, len);
    fsync(log_file);
}


/*
    Log a message with verbosity level informational.
*/
void flog_info(
    const char * s
){
    if(log_lvl < LOG_INFORM)
        return;

    open_log_file();
    u32 len = strlen(s);
    write(log_file, s, len);
    fsync(log_file);
}


