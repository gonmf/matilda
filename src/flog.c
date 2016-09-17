/*
Support for logging to file. Logging is made to a file called
matilda_YYMMDD_XXXXXX.log where YYMMDD is the date and XXXXXX is a random
string. When logging a mask of log categories specifies the types of messages to
be written to file. Having a very high degree of detail in very fast matches
actively hurts the performance.

Writing to files is synchronous (with fsync) to avoid loss of data in case of
crashes, but it is impossible to guarantee this in all cases.
*/

#include "matilda.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h> /* mkstemps in macosx */
#include <stdlib.h> /* mkstemps */
#include <time.h> /* localtime */

#include "alloc.h"
#include "amaf_rave.h"
#include "engine.h"
#include "flog.h"
#include "mcts.h"
#include "pat3.h"
#include "playout.h"
#include "scoring.h"
#include "time_ctrl.h"
#include "timem.h"
#include "types.h"
#include "version.h"

static int log_file = -1;
static char log_filename[32];
/*
By default print everything to standard output. Only the main matilda executable
changes this by default.
*/
static u16 log_mode = (LOG_MODE_ERROR | LOG_MODE_WARN | LOG_MODE_PROT |
    LOG_MODE_INFO | LOG_MODE_DEBUG);
static u16 log_dest = LOG_DEST_STDF;

/*
For non-default values for build_info
*/
extern u64 max_size_in_mbs;
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
extern u16 prior_corner;
extern double rave_equiv;
extern u16 pl_skip_saving;
extern u16 pl_skip_nakade;
extern u16 pl_skip_pattern;
extern u16 pl_skip_capture;
extern u16 pl_ban_self_atari;
extern d16 komi;

static void open_log_file();

static void flog(
    const char * severity,
    const char * context,
    const char * msg
);

/*
Sets the logging messages that are written to file based on a mask of the
combination of available message types. See flog.h for more information.
*/
void flog_config_modes(
    u16 new_mode
){
    if(new_mode == log_mode)
        return;

    if(new_mode != 0)
    {
        if(log_file != -1)
        {
            log_mode = new_mode;

            char * s = alloc();
            u32 idx = 0;
            idx += snprintf(s + idx, MAX_PAGE_SIZ - idx, "log mask changed: ");
            if(log_mode == 0)
                snprintf(s + idx, MAX_PAGE_SIZ - idx, "none");
            else
            {
                if(log_mode & LOG_MODE_ERROR)
                    idx += snprintf(s + idx, MAX_PAGE_SIZ - idx, "crit,");
                if(log_mode & LOG_MODE_WARN)
                    idx += snprintf(s + idx, MAX_PAGE_SIZ - idx, "warn,");
                if(log_mode & LOG_MODE_PROT)
                    idx += snprintf(s + idx, MAX_PAGE_SIZ - idx, "prot,");
                if(log_mode & LOG_MODE_INFO)
                    idx += snprintf(s + idx, MAX_PAGE_SIZ - idx, "info,");
                if(log_mode & LOG_MODE_DEBUG)
                    idx += snprintf(s + idx, MAX_PAGE_SIZ - idx, "dbug,");
                s[idx - 1] = 0;
            }
            flog(NULL, NULL, s);
            release(s);
            return;
        }
    }
    else
    {
        if(log_file != -1)
        {
            flog(NULL, NULL, "logging disabled");
            close(log_file);
        }
        log_file = -1;
    }

    log_mode = new_mode;
}

/*
    Define the destinations for logging.
*/
void flog_config_destinations(
    u16 new_dest
){
    log_dest = new_dest;
}

static bool ends_in_new_line(
    const char * s
){
    u32 l = strlen(s);
    return (l > 0) && (s[l - 1] == '\n');
}

static bool multiline(
    const char * s
){
    char * t = strchr(s, '\n');
    return !(t == NULL || t == s + (strlen(s) - 1));
}

static void flog(
    const char * severity,
    const char * context,
    const char * msg
){
    if(!log_dest)
        return;

    // Prepare payload
    char * s = alloc();
    char * ts = alloc();
    timestamp(ts);

    if(severity == NULL)
        severity = "    ";
    if(context == NULL)
        context = "    ";

    if(multiline(msg))
    {
        snprintf(s, MAX_PAGE_SIZ, "%22s | %4s | %4s | [\n%s%s]\n", ts, severity,
            context, msg, ends_in_new_line(msg) ? "" : "\n");
    }
    else
    {
        snprintf(s, MAX_PAGE_SIZ, "%22s | %4s | %4s | %s%s", ts, severity,
            context, msg, ends_in_new_line(msg) ? "" : "\n");
    }

    if(log_dest & LOG_DEST_STDF)
    {
        fprintf(stderr, "%s", s);
    }

    if(log_dest & LOG_DEST_FILE)
    {
        open_log_file();
        u32 len = strlen(s);
        write(log_file, s, len);
        fsync(log_file);
    }

    release(ts);
    release(s);
}

static void open_log_file()
{
    if(log_file == -1)
    {
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        snprintf(log_filename, 32, "matilda_%02u%02u%02u_XXXXXX.log", tm.tm_year
        % 100, tm.tm_mon, tm.tm_mday);
        log_file = mkstemps(log_filename, 4);
        if(log_file == -1)
        {
            fprintf(stderr, "Failed to generate random log file name.\n");
            snprintf(log_filename, 32, "matilda_%02u%02u%02u.log",
                tm.tm_year % 100, tm.tm_mon, tm.tm_mday);
        }

        char * s = alloc();
        u32 idx = 0;
        idx += snprintf(s + idx, MAX_PAGE_SIZ - idx,
            "logging enabled with mask: ");
        if(log_mode == 0)
            snprintf(s + idx, MAX_PAGE_SIZ - idx, "none");
        else
        {
            if(log_mode & LOG_MODE_ERROR)
                idx += snprintf(s + idx, MAX_PAGE_SIZ - idx, "crit,");
            if(log_mode & LOG_MODE_WARN)
                idx += snprintf(s + idx, MAX_PAGE_SIZ - idx, "warn,");
            if(log_mode & LOG_MODE_PROT)
                idx += snprintf(s + idx, MAX_PAGE_SIZ - idx, "prot,");
            if(log_mode & LOG_MODE_INFO)
                idx += snprintf(s + idx, MAX_PAGE_SIZ - idx, "info,");
            if(log_mode & LOG_MODE_DEBUG)
                idx += snprintf(s + idx, MAX_PAGE_SIZ - idx, "dbug,");
            s[idx - 1] = 0;
        }

        flog(NULL, NULL, s);
        release(s);
    }
}


/*
Obtain a textual description of the capabilities and configuration options of
matilda. This mostly concerns compile time constants.
RETURNS string with build information
*/
void build_info(
    char * dst
){
    u32 idx = 0;
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "Matilda build information\n");
    if(MATILDA_RELEASE_MODE)
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "Compiled for: release\n");
    else
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "Compiled for: debugging\n");
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "Version: %s\n",
        MATILDA_VERSION);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "Data folder: %s\n",
        get_data_folder());

    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "Board size: %ux%u\n",
        BOARD_SIZ, BOARD_SIZ);

    char * kstr = alloc();
    komi_to_string(kstr, komi);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "Komidashi: %s stones\n",
        kstr);
    release(kstr);

    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "MCTS-UCT branch limiter: %s\n", YN(USE_UCT_BRANCH_LIMITER));
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "Can resign: %s\n",
        YN(CAN_RESIGN));
    if(CAN_RESIGN)
    {
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "  Bellow win rate: %.2f\n", UCT_RESIGN_WINRATE);
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "  Minimum simulations: %u\n", UCT_RESIGN_PLAYOUTS);
    }
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "Can stop MCTS early: %s\n",
        YN(UCT_CAN_STOP_EARLY));
    if(UCT_CAN_STOP_EARLY)
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "  At win rate: %.2f\n",
            UCT_EARLY_WINRATE);

    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "Winrate for passing always: %.2f\n", JUST_PASS_WINRATE);

    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "Transpositions table memory: %" PRIu64 " MiB\n", max_size_in_mbs);

    if(pl_skip_saving)
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "  Chance of skipping save: %u/128\n", pl_skip_saving);
    if(pl_skip_capture)
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "  Chance of skipping capture: %u/128\n", pl_skip_capture);
    if(pl_skip_pattern)
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "  Chance of skipping pattern: %u/128\n", pl_skip_pattern);
    if(pl_skip_nakade)
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "  Chance of skipping nakade: %u/128\n", pl_skip_nakade);
    if(pl_ban_self_atari)
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "  Chance of prohibiting self-atari: %u/128\n", pl_ban_self_atari);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "  Use pattern weights: %s\n", YN(USE_PATTERN_WEIGHTS));
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "Use AMAF/RAVE: %s\n", YN(USE_AMAF_RAVE));
    if(USE_AMAF_RAVE)
    {
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "  MSE equiv: %.2f\n", rave_equiv);
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "  Criticality threshold: %u\n", CRITICALITY_THRESHOLD);
    }
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "  Stone value scale factor: %.1f\n", prior_stone_scale_factor);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "  Even: %u\n",
        prior_even);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "  Nakade: %u\n",
        prior_nakade);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "  Self-atari: -%u\n",
        prior_self_atari);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "  Attack 1/2 lib group: %u\n", prior_attack);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "  Defend 1/2 lib group: %u\n", prior_defend);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "  MoGo patterns: %u\n",
        prior_pat3);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "  Near last play: %u\n",
        prior_near_last);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "  Empty L2/3/other: -%u/%u/%u\n", prior_line2, prior_line3,
        prior_empty);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "  Corners: -%u\n",
        prior_corner);

    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "Max UCT depth: %u\n",
        MAX_UCT_DEPTH);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "UCT expansion delay: %u\n", UCT_EXPANSION_DELAY);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "Playout depth over number of empty points: %u\n",
        MAX_PLAYOUT_DEPTH_OVER_EMPTY);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "Mercy threshold: %u stones\n", MERCY_THRESHOLD);

    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "Detect network latency: %s\n", YN(DETECT_NETWORK_LATENCY));
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "Constant latency compensation: %u ms\n", LATENCY_COMPENSATION);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "Time allotment factor: %.2f\n", TIME_ALLOT_FACTOR);

    u32 num_threads;
    #pragma omp parallel
    #pragma omp master
    {
        num_threads = omp_get_num_threads();
    }
    if(DEFAULT_NUM_THREADS == 0)
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "Default number of threads: automatic (%u)\n", num_threads);
    else
        idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
            "Default number of threads: %u (%u)\n", DEFAULT_NUM_THREADS,
            num_threads);
    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx,
        "Maximum number of threads: %u\n", MAXIMUM_NUM_THREADS);
    snprintf(dst + idx, MAX_PAGE_SIZ - idx, "\n");
}

/*
Log a message with verbosity level critical.
*/
void flog_crit(
    const char * ctx,
    const char * msg
){
    if((log_mode & LOG_MODE_ERROR) != 0)
    {
        flog("crit", ctx, msg);
        flog(NULL, NULL, "execution aborted due to program panic");
    }

    exit(EXIT_FAILURE);
}


/*
Log a message with verbosity level warning.
*/
void flog_warn(
    const char * ctx,
    const char * msg
){
    if((log_mode & LOG_MODE_WARN) != 0)
        flog("warn", ctx, msg);
}


/*
Log a message with verbosity level communication protocol.
*/
void flog_prot(
    const char * ctx,
    const char * msg
){
    if((log_mode & LOG_MODE_PROT) != 0)
        flog("prot", ctx, msg);
}


/*
    Log a message with verbosity level informational.
*/
void flog_info(
    const char * ctx,
    const char * msg
){
    if((log_mode & LOG_MODE_INFO) != 0)
        flog("info", ctx, msg);
}


/*
    Log a message with verbosity level debug.
*/
void flog_dbug(
    const char * ctx,
    const char * msg
){
    if((log_mode & LOG_MODE_DEBUG) != 0)
        flog("dbug", ctx, msg);
}


