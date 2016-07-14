/*
Specification of helper functions for reading .pts files, which have rules in a
format similar to Fuego-style opening books; plus some useful functions for
reading handicap, hoshi and starting plays for MCTS.
*/

#include "matilda.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "move.h"
#include "board.h"
#include "state_changes.h"
#include "file_io.h"
#include "flog.h"
#include "engine.h"
#include "timem.h"
#include "stringm.h"
#include "buffer.h"

static bool handicap_points_attempted_load = false;
static bool hoshi_points_attempted_load = false;
static bool starting_points_attempted_load = false;
static move_seq handicap;
static move_seq hoshi;
static move_seq starting;

/*
State for interpreting rule files one by one.
*/
static char * buffer = NULL;
static char * save_ptr;
static bool search_started = false;


/*
Open and prepare a file to be interpreted line by line.
*/
void open_rule_file(
    const char * filename
){
    if(buffer != NULL)
    {
        fprintf(stderr, "error: pts_file: file open\n");
        flog_crit("error: pts_file: file open\n");
        exit(EXIT_FAILURE);
    }

    char * fn = get_buffer();
    if(starts_with(filename, get_data_folder()))
        snprintf(fn, MAX_PAGE_SIZ, "%s", filename);
    else
        snprintf(fn, MAX_PAGE_SIZ, "%s%s", get_data_folder(), filename);

    buffer = malloc(MAX_FILE_SIZ);
    if(buffer == NULL)
    {
        fprintf(stderr, "error: pts_file: system out of memory\n");
        flog_crit("error: pts_file: system out of memory\n");
        exit(EXIT_FAILURE);
    }

    s32 chars_read = read_ascii_file(fn, buffer, MAX_FILE_SIZ);
    if(chars_read < 0)
    {
        fprintf(stderr, "error: pts_file: couldn't open file for reading\n");
        flog_crit("error: pts_file: couldn't open file for reading\n");
        exit(EXIT_FAILURE);
    }

    search_started = false;
}

/*
Read the next rule line.
RETURNS rule line string
*/
char * read_next_rule(){
    if(buffer == NULL)
    {
        fprintf(stderr, "error: pts_file: no file open\n");
        flog_crit("error: pts_file: no file open\n");
        exit(EXIT_FAILURE);
    }

    char * line;
    if(search_started)
        line = strtok_r(NULL, "\r\n", &save_ptr);
    else
    {
        line = strtok_r(buffer, "\r\n", &save_ptr);
        search_started = true;
    }

    if(line == NULL)
        return NULL;

    line_cut_before(line, '#');
    line = trim(line);
    if(line == NULL)
        return read_next_rule();

    u16 len = strlen(line);
    if(len == 0)
        return read_next_rule();

    char * save_ptr2 = NULL;
    char * word = strtok_r(line, " ", &save_ptr2);
    if(strcmp(word, BOARD_SIZ_AS_STR) != 0)
        return read_next_rule();

    char * ret = get_buffer();
    strncpy(ret, line + strlen(BOARD_SIZ_AS_STR) + 1, MAX_PAGE_SIZ);
    return ret;
}

/*
Interpret a string as a rule line, filling a move_seq structure with the points.
*/
void interpret_rule_as_pts_list(
    move_seq * dst,
    const char * src
){
    u16 tokens_read = 0;
    char tokens[BOARD_SIZ * BOARD_SIZ][4];

    char * tmp = get_buffer();
    strncpy(tmp, src, MAX_PAGE_SIZ);

    char * word;
    char * save_ptr2 = NULL;
    if((word = strtok_r(tmp, " ", &save_ptr2)) != NULL)
        strncpy(tokens[tokens_read++], word, 4);

    while(tokens_read < BOARD_SIZ * BOARD_SIZ && (word = strtok_r(NULL, " ",
        &save_ptr2)) != NULL)
        strncpy(tokens[tokens_read++], word, 4);

    if(tokens_read < 1 || tokens_read == BOARD_SIZ * BOARD_SIZ)
    {
        char * buf = get_buffer();
        snprintf(buf, MAX_PAGE_SIZ, "error: pts_file: malformed line: %s\n",
            src);
        fprintf(stderr, "%s", buf);
        flog_crit(buf);
        exit(EXIT_FAILURE);
    }

    board b;
    clear_board(&b);
    dst->count = 0;

    for(u16 t = 0; t < tokens_read; ++t)
    {
        move m = coord_parse_alpha_num(tokens[t]);
        if(!is_board_move(m))
        {
            char * buf = get_buffer();
            snprintf(buf, MAX_PAGE_SIZ, "error: pts_file: malformed line: %s\n",
                src);
            fprintf(stderr, "%s", buf);
            flog_crit(buf);
            exit(EXIT_FAILURE);
        }
        if(!attempt_play_slow(&b, m, true))
        {
            char * buf = get_buffer();
            snprintf(buf, MAX_PAGE_SIZ, "error: pts_file: malformed line: %s\n",
                src);
            fprintf(stderr, "%s", buf);
            flog_crit(buf);
            exit(EXIT_FAILURE);
        }

        add_move(dst, m);
    }
}

/*
Close the rule file previously opened.
*/
void close_rule_file()
{
    free(buffer);
    buffer = NULL;
}



static void load_points(
    const char * name,
    move_seq * dst
){
    dst->count = 0;

    char * buf = get_buffer();
    snprintf(buf, MAX_PAGE_SIZ, "%s.pts", name);
    open_rule_file(buf);

    char * s;
    if((s = read_next_rule()) != NULL)
    {
        interpret_rule_as_pts_list(dst, s);

        char * buf = get_buffer();
        snprintf(buf, MAX_PAGE_SIZ, "%s: pts_file: loaded %u %s points\n",
            timestamp(), dst->count, name);
        fprintf(stderr, "%s", buf);
        flog_info(buf);
    }

    close_rule_file();
}


/*
Load handicap points.
*/
void load_handicap_points()
{
    if(handicap_points_attempted_load)
        return;

    load_points("handicap", &handicap);
    handicap_points_attempted_load = true;
}

/*
Load hoshi points.
*/
void load_hoshi_points()
{
    if(hoshi_points_attempted_load)
        return;

    load_points("hoshi", &hoshi);
    hoshi_points_attempted_load = true;
}

/*
Load starting MCTS points.
*/
void load_starting_points()
{
    if(starting_points_attempted_load)
        return;

    load_points("starting", &starting);
    starting_points_attempted_load = true;
}



/*
Retrieve an ordered list of suggested handicap points.
*/
void get_ordered_handicap(
    move_seq * dst
){
    load_handicap_points();

    memcpy(dst->coord, handicap.coord, handicap.count * sizeof(move));
    dst->count = handicap.count;
}

/*
Tests if a point is hoshi.
RETURNS true if hoshi
*/
bool is_hoshi_point(
    move m
){
    load_hoshi_points();

    for(move i = 0; i < hoshi.count; ++i)
        if(hoshi.coord[i] == m)
            return true;

    return false;
}

/*
Retrieve a list of starting points for MCTS.
*/
void get_starting_points(
    move_seq * dst
){
    load_starting_points();

    memcpy(dst->coord, starting.coord, starting.count * sizeof(move));
    dst->count = starting.count;
}

