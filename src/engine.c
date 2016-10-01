/*
Functions that control the flow of information of Matilda as a complete Go
playing program. Allows executing strategies with some abstraction, performing
maintenance if needed.
*/

#include "matilda.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h> /* opendir */
#include <dirent.h> /* opendir */

#include "alloc.h"
#include "board.h"
#include "cfg_board.h"
#include "flog.h"
#include "game_record.h"
#include "mcts.h"
#include "opening_book.h"
#include "transpositions.h"
#include "types.h"
#include "version.h"

static bool use_opening_book = true;

bool tt_requires_maintenance = false; /* set after MCTS start/resume call */


static char _data_folder[MAX_PATH_SIZ] = DEFAULT_DATA_PATH;

/*
Produce a short version string. Does not include program name.
*/
void version_string(
    char * dst
){
    snprintf(dst, MAX_PAGE_SIZ, "%s (%ux%u)", MATILDA_VERSION, BOARD_SIZ, BOARD_SIZ);
}

/*
Obtains the current data folder path. It may be absolute or relative and ends
with a path separator.
RETURNS folder path
*/
const char * get_data_folder()
{
    return _data_folder;
}

/*
Sets the new data folder path. If the path is too long, short, or otherwise
invalid, nothing is changed and false is returned.
RETURNS true on success
*/
bool set_data_folder(
    const char * s
){
    u32 l = strlen(s);
    if(l < 2 || l >= MAX_PATH_SIZ - 1)
        return false;

    if(s[l - 1] == '/')
        snprintf(_data_folder, MAX_PATH_SIZ, "%s", s);
    else
        snprintf(_data_folder, MAX_PATH_SIZ, "%s/", s);
    return true;
}

/*
Set whether to attempt to use, or not, opening books prior to MCTS.
*/
void set_use_of_opening_book(
    bool use_ob
){
    use_opening_book = use_ob;
}

/*
Evaluates the position given the time available to think, by using a number of
strategies in succession.
RETURNS true if a play or pass is suggested instead of resigning
*/
bool evaluate_position_timed(
    const board * b,
    bool is_black,
    out_board * out_b,
    u64 stop_time,
    u64 early_stop_time
){
    if(use_opening_book)
    {
        board tmp;
        memcpy(&tmp, b, sizeof(board));
        d8 reduction = reduce_auto(&tmp, true);
        if(opening_book(out_b, &tmp))
        {
            oboard_revert_reduce(out_b, reduction);
            return true;
        }
    }

    bool ret = mcts_start_timed(out_b, b, is_black, stop_time, early_stop_time);
    tt_requires_maintenance = true;
    return ret;
}

/*
Evaluates the position with the number of simulations available.
RETURNS true if a play or pass is suggested instead of resigning
*/
bool evaluate_position_sims(
    const board * b,
    bool is_black,
    out_board * out_b,
    u32 simulations
){
    if(use_opening_book)
    {
        board tmp;
        memcpy(&tmp, b, sizeof(board));
        d8 reduction = reduce_auto(&tmp, is_black);
        if(opening_book(out_b, &tmp))
        {
            oboard_revert_reduce(out_b, reduction);
            return true;
        }
    }

    bool ret = mcts_start_sims(out_b, b, is_black, simulations);
    tt_requires_maintenance = true;
    return ret;
}

/*
Evaluate the position for a short amount of time, ignoring the quality matrix
produced.
*/
void evaluate_in_background(
    const board * b,
    bool is_black
){
    mcts_resume(b, is_black);
    tt_requires_maintenance = true;
}

/*
Inform that we are currently between matches and proceed with the maintenance
that is suitable at the moment.
*/
void new_match_maintenance()
{
    u32 freed = tt_clean_all();
    tt_requires_maintenance = false;

    if(freed > 0)
    {
        char * s = alloc();
        snprintf(s, MAX_PAGE_SIZ,
            "freed all %u states between matches (%lu MiB)", freed,
            (freed * sizeof(tt_stats)) / 1048576);
        flog_info("mcts", s);
        release(s);
    }
}

/*
Perform between-turn maintenance. If there is any information from MCTS-UCT that
can be freed, it will be done to the states not reachable from state b played by
is_black.
*/
void opt_turn_maintenance(
    const board * b,
    bool is_black
){
    if(tt_requires_maintenance)
    {
        u32 freed = tt_clean_outside_tree(b, is_black);
        tt_requires_maintenance = false;

        if(freed > 0)
        {
            char * s = alloc();
            snprintf(s, MAX_PAGE_SIZ, "freed %u states (%lu MiB)\n", freed,
                (freed * sizeof(tt_stats)) / 1048576);
            flog_info("mcts", s);
            release(s);
        }
    }
}

/*
Asserts the ./data folder exists, closing the program and warning the user if it
doesn't.
*/
void assert_data_folder_exists()
{
    DIR * dir = opendir(get_data_folder());
    if(dir == NULL)
    {
        char * s = alloc();
        snprintf(s, MAX_PAGE_SIZ, "data folder %s does not exist or is unavaila\
ble\n", get_data_folder());
        flog_crit("data", s);
        release(s);
    }else
        closedir(dir);
}






