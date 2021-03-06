/*
Specification of helper functions for reading .pts files, which have rules in a
format similar to Fuego-style opening books; plus some useful functions for
reading handicap, hoshi and starting plays for MCTS.
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "board.h"
#include "engine.h"
#include "file_io.h"
#include "flog.h"
#include "move.h"
#include "state_changes.h"
#include "stringm.h"
#include "types.h"

static bool handicap_points_attempted_load = false;
static bool hoshi_points_attempted_load = false;
static bool starting_points_attempted_load = false;

static move_seq handicap;
static move_seq hoshi;
static move_seq starting;

bool is_handicap[TOTAL_BOARD_SIZ];
bool is_hoshi[TOTAL_BOARD_SIZ];
bool is_starting[TOTAL_BOARD_SIZ];

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
) {
    if (buffer != NULL) {
        flog_crit("ptsf", "error: pts_file: file open");
    }

    char * fn = alloc();
    if (starts_with(filename, data_folder())) {
        snprintf(fn, MAX_PAGE_SIZ, "%s", filename);
    } else {
        snprintf(fn, MAX_PAGE_SIZ, "%s%s", data_folder(), filename);
    }

    buffer = malloc(MAX_FILE_SIZ);
    if (buffer == NULL) {
        flog_crit("ptsf", "system out of memory");
    }


    d32 chars_read = read_ascii_file(buffer, MAX_FILE_SIZ, fn);
    if (chars_read < 0) {
        flog_crit("ptsf", "couldn't open file for reading");
    }

    release(fn);
    search_started = false;
}

/*
Read the next rule line.
*/
void read_next_rule(
    char * dst
) {
    if (buffer == NULL) {
        flog_crit("ptsf", "no file open");
    }

    char * line;
    if (search_started) {
        line = strtok_r(NULL, "\r\n", &save_ptr);
    } else {
        line = strtok_r(buffer, "\r\n", &save_ptr);
        search_started = true;
    }

    if (line == NULL) {
        dst[0] = 0;
        return;
    }

    line_cut_before(line, '#');
    line = trim(line);
    if (line == NULL) {
        read_next_rule(dst);
        return;
    }

    u16 len = strlen(line);
    if (len == 0) {
        read_next_rule(dst);
        return;
    }

    char * save_ptr2 = NULL;
    char * word = strtok_r(line, " ", &save_ptr2);
    if (strcmp(word, BOARD_SIZ_AS_STR) != 0) {
        read_next_rule(dst);
        return;
    }

    strncpy(dst, line + strlen(BOARD_SIZ_AS_STR) + 1, MAX_PAGE_SIZ);
}

/*
Interpret a string as a rule line, filling a move_seq structure with the points.
*/
void interpret_rule_as_pts_list(
    move_seq * dst,
    const char * src
) {
    u16 tokens_read = 0;
    char tokens[TOTAL_BOARD_SIZ][4];

    char * tmp = alloc();
    strncpy(tmp, src, MAX_PAGE_SIZ);

    char * word;
    char * save_ptr2 = NULL;
    if ((word = strtok_r(tmp, " ", &save_ptr2)) != NULL) {
        strncpy(tokens[tokens_read++], word, 4);
    }

    while (tokens_read < TOTAL_BOARD_SIZ && (word = strtok_r(NULL, " ", &save_ptr2)) != NULL) {
        strncpy(tokens[tokens_read++], word, 4);
    }

    if (tokens_read < 1 || tokens_read == TOTAL_BOARD_SIZ) {
        char * buf = alloc();
        snprintf(buf, MAX_PAGE_SIZ, "malformed line: %s", src);
        flog_crit("ptsf", buf);
        release(buf);
    }

    board b;
    clear_board(&b);
    dst->count = 0;

    for (u16 t = 0; t < tokens_read; ++t) {
        move m = coord_parse_alpha_num(tokens[t]);

        if (!is_board_move(m)) {
            char * buf = alloc();
            snprintf(buf, MAX_PAGE_SIZ, "malformed line: %s", src);
            flog_crit("ptsf", buf);
            release(buf);
        }

        if (!attempt_play_slow(&b, true, m)) {
            char * buf = alloc();
            snprintf(buf, MAX_PAGE_SIZ, "malformed line: %s", src);
            flog_crit("ptsf", buf);
            release(buf);
        }

        add_move(dst, m);
    }

    release(tmp);
}

/*
Close the rule file previously opened.
*/
void close_rule_file() {
    free(buffer);
    buffer = NULL;
}



static void load_points(
    const char * name,
    move_seq * dst
) {
    dst->count = 0;

    char * buf = alloc();
    snprintf(buf, MAX_PAGE_SIZ, "%s.pts", name);
    open_rule_file(buf);

    char * s = alloc();
    read_next_rule(s);

    if (s != NULL) {
        interpret_rule_as_pts_list(dst, s);

        snprintf(buf, MAX_PAGE_SIZ, "loaded %u %s points", dst->count, name);
        flog_info("ptsf", buf);
    }

    release(buf);
    release(s);
    close_rule_file();
}


/*
Load handicap points.
*/
void load_handicap_points() {
    if (handicap_points_attempted_load) {
        return;
    }

    load_points("handicap", &handicap);

    memset(is_handicap, false, TOTAL_BOARD_SIZ);
    for (move i = 0; i < handicap.count; ++i) {
        is_handicap[handicap.coord[i]] = true;
    }

    handicap_points_attempted_load = true;
}

/*
Load hoshi points.
*/
void load_hoshi_points() {
    if (hoshi_points_attempted_load) {
        return;
    }

    load_points("hoshi", &hoshi);

    memset(is_hoshi, false, TOTAL_BOARD_SIZ);
    for (move i = 0; i < hoshi.count; ++i) {
        is_hoshi[hoshi.coord[i]] = true;
    }

    hoshi_points_attempted_load = true;
}

/*
Load starting MCTS points.
*/
void load_starting_points() {
    if (starting_points_attempted_load) {
        return;
    }

    load_points("starting", &starting);

    memset(is_starting, false, TOTAL_BOARD_SIZ);
    for (move i = 0; i < starting.count; ++i) {
        is_starting[starting.coord[i]] = true;
    }

    starting_points_attempted_load = true;
}



/*
Retrieve an ordered list of suggested handicap points.
*/
void get_ordered_handicap(
    move_seq * dst
) {
    load_handicap_points();

    memcpy(dst->coord, handicap.coord, handicap.count * sizeof(move));
    dst->count = handicap.count;
}

