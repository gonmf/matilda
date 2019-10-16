/*
Functions related to reading and writing SGF FF[4] files.
http://www.red-bean.com/sgf/

Play variations and annotations/commentary are ignored.
*/

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "alloc.h"
#include "board.h"
#include "file_io.h"
#include "flog.h"
#include "game_record.h"
#include "scoring.h"
#include "state_changes.h"
#include "stringm.h"
#include "types.h"
#include "version.h"

extern d16 komi;

static bool undeclared_game_ruleset_warned = false;
static bool board_size_cant_be_guessed_warned = false;
static bool sgf_format_undeclared_warned = false;
static bool undeclared_board_size_warned = false;
static bool wrong_board_size_warned = false;
static bool illegal_final_score_warned = false;
static bool illegal_handicap_placement_warned = false;
static bool illegal_stone_placement_warned = false;
static bool komi_format_error = false;

static u8 guess_board_size(
    const char * sgf
) {
    char buf[8];
    for (u8 size = 23; size > 4; size -= 2) {
        snprintf(buf, 8, ";B[%c", 'a' + size - 1);
        if (strstr(sgf, buf) != NULL)
            return size;
        snprintf(buf, 8, ";W[%c", 'a' + size - 1);
        if (strstr(sgf, buf) != NULL)
            return size;
    }
    return 0;
}

/*
Writes a game record to a buffer of size length, to the best of the available
information.
RETURNS chars written
*/
u32 export_game_as_sgf_to_buffer(
    const game_record * gr,
    char * buffer,
    u32 size
) {
    char * buf = buffer;
    buf += snprintf(buf, size - (buf - buffer), "(;GM[1]\n");
    buf += snprintf(buf, size - (buf - buffer), "FF[4]\n");
    buf += snprintf(buf, size - (buf - buffer), "CA[UTF-8]\n");
    buf += snprintf(buf, size - (buf - buffer), "SZ[%u]\n", BOARD_SIZ);
    buf += snprintf(buf, size - (buf - buffer), "PW[%s]\n", gr->white_name);
    buf += snprintf(buf, size - (buf - buffer), "PB[%s]\n", gr->black_name);

    char * kstr = alloc();
    komi_to_string(kstr, komi);
    buf += snprintf(buf, size - (buf - buffer), "KM[%s]\n", kstr);
    release(kstr);

    if (gr->finished) {
        if (gr->resignation)
            buf += snprintf(buf, size - (buf - buffer), "RE[%c+R]\n",
                gr->final_score > 0 ? 'B' : 'W');
        else
            buf += snprintf(buf, size - (buf - buffer), "RE[%c+%u.5]\n",
                gr->final_score > 0 ? 'B' : 'W', abs(gr->final_score) / 2);
    }
    else
        buf += snprintf(buf, size - (buf - buffer), "RE[Void]\n");

    /* Not standard but as used in KGS; closest would be AGA rules */
    buf += snprintf(buf, size - (buf - buffer), "RU[Chinese]\n");
    buf += snprintf(buf, size - (buf - buffer), "AP[matilda:%s]\n",
        MATILDA_VERSION);


    char * mstr = alloc();

    /* Handicap stones */
    if (gr->handicap_stones.count > 1) {
        buf += snprintf(buf, size - (buf - buffer), "HA[%u]\nAB",
            gr->handicap_stones.count);
        for (u16 i = 0; i < gr->handicap_stones.count; ++i) {
            coord_to_alpha_alpha(mstr, gr->moves[i]);
            buf += snprintf(buf, size - (buf - buffer), "[%s]\n", mstr);
        }
    }

    /* Plays */
    for (u16 i = 0; i < gr->turns; ++i) {
        if (i > 0 && (i % 10) == 0)
            buf += snprintf(buf, size - (buf - buffer), "\n");

        assert(gr->moves[i] != NONE);
        if (gr->moves[i] == PASS) {
            if (gr->handicap_stones.count == 0)
                buf += snprintf(buf, size - (buf - buffer), ";%c[]", (i & 1) ==
                    0 ? 'B' : 'W');
            else
                buf += snprintf(buf, size - (buf - buffer), ";%c[]", (i & 1) ==
                    1 ? 'B' : 'W');
        }
        else
        {
            coord_to_alpha_alpha(mstr, gr->moves[i]);
            if (gr->handicap_stones.count == 0)
            {
                buf += snprintf(buf, size - (buf - buffer), ";%c[%s]", (i & 1)
                    == 0 ? 'B' : 'W', mstr);
            }
            else
            {
                coord_to_alpha_alpha(mstr, gr->moves[i]);
                buf += snprintf(buf, size - (buf - buffer), ";%c[%s]", (i & 1)
                    == 1 ? 'B' : 'W', mstr);
            }
        }
    }
    buf += snprintf(buf, size - (buf - buffer), ")\n");
    release(mstr);

    return buf - buffer;
}

/*
Writes a game record to SGF file with automatically generated name, to the best
of the available information.
Writes the file name generated to the buffer provided.
RETURNS false on error
*/
bool export_game_as_sgf_auto_named(
    const game_record * gr,
    char filename[static MAX_PAGE_SIZ]
) {
    int fid = create_and_open_file(filename, MAX_PAGE_SIZ, "matilda", "sgf");
    if (fid == -1)
        return false;

    char * buffer = alloc();
    if (buffer == NULL) {
        close(fid);
        return false;
    }

    u32 chars = export_game_as_sgf_to_buffer(gr, buffer, MAX_PAGE_SIZ);

    write(fid, buffer, chars);
    close(fid);

    release(buffer);
    return true;
}

/*
Writes a game record to SGF file, to the best of the available information.
Fails if the file already exists.
RETURNS false on error
*/
bool export_game_as_sgf(
    const game_record * gr,
    const char * filename
) {
    FILE * fp = fopen(filename, "r");
    if (fp != NULL) {
        fprintf(stderr, "file already exists\n");
        fclose(fp);
        return false;
    }
    fp = fopen(filename, "w");
    if (fp == NULL)
        return false;

    char * buffer = alloc();
    if (buffer == NULL)
        return false;

    u32 chars = export_game_as_sgf_to_buffer(gr, buffer, 4 * 1024);
    fwrite(buffer, sizeof(char), chars, fp);

    fclose(fp);
    release(buffer);
    return true;
}

/*
Reset the printing of warning messages. Since the process of processing SGF
files can be repetitive a lot of noise could be produced. By default warning
messages are only shown the first time a particular type of problem is found
with the files. Use this function to reset the warnings to be shown.
*/
void reset_warning_messages() {
    undeclared_game_ruleset_warned = false;
    board_size_cant_be_guessed_warned = false;
    sgf_format_undeclared_warned = false;
    undeclared_board_size_warned = false;
    wrong_board_size_warned = false;
    illegal_final_score_warned = false;
    illegal_handicap_placement_warned = false;
    illegal_stone_placement_warned = false;
}

/*
Import a game record from the contents of the buffer.
RETURNS true if the game has been found and read correctly
*/
bool import_game_from_sgf2(
    game_record * gr,
    const char * restrict filename,
    char * restrict buf,
    u32 buf_siz
) {
    clear_game_record(gr);

    d32 chars_read = read_ascii_file(buf, buf_siz, filename);
    if (chars_read < 1) {
        snprintf(buf, MAX_PAGE_SIZ, "could not open/read file %s", filename);
        flog_warn("sgff", buf);
        return false;
    }

    /*
    Game
    */
    if (strstr(buf, "GM[1]") == NULL)
        if (!sgf_format_undeclared_warned) {
            sgf_format_undeclared_warned = true;
            flog_warn("sgff", "GM[1] annotation not found");
        }

    char * tmp = alloc();

    /*
    Komi
    */
    str_between(tmp, buf, "KM[", "]");
    if (tmp[0] != 0) {
        double komid;
        if (!parse_float(&komid, tmp)) {
            if (!komi_format_error)
            {
                komi_format_error = true;
                flog_warn("sgff", "komi format error; current komi kept");
            }
        }
        else
        {
            komi = (d16)(komid * 2.0);
        }
    }

    /*
    Board size
    */
    str_between(tmp, buf, "SZ[", "]");
    if (tmp[0] == 0) {
        if (!undeclared_board_size_warned) {
            undeclared_board_size_warned = true;
            flog_warn("sgff", "board size not specified");
        }

        u8 board_size = guess_board_size(buf);
        if (board_size == 0 && !board_size_cant_be_guessed_warned) {
            board_size_cant_be_guessed_warned = true;
            flog_warn("sgff", "board size can not be guessed from play coordinates");
        }
        if (board_size != BOARD_SIZ && board_size + 1 != BOARD_SIZ) {
            if (!wrong_board_size_warned) {
                wrong_board_size_warned = true;
                flog_warn("sgff", "wrong board size");
            }
            release(tmp);
            return false;
        }
    }
    else
        if (strcmp(tmp, BOARD_SIZ_AS_STR) != 0) {
            if (!wrong_board_size_warned) {
                wrong_board_size_warned = true;
                flog_warn("sgff", "wrong board size");
            }
            release(tmp);
            return false;
        }

    /*
    Player names
    */

    str_between(tmp, buf, "PB[", "]");
    if (tmp[0]) {
        strncpy(gr->black_name, tmp, MAX_PLAYER_NAME_SIZ);
        gr->player_names_set = true;
    }

    str_between(tmp, buf, "PW[", "]");
    if (tmp[0]) {
        strncpy(gr->white_name, tmp, MAX_PLAYER_NAME_SIZ);
        gr->player_names_set = true;
    }

    /*
    Result
    */
    bool finished = false;
    bool resignation = false;
    bool timeout = false;
    d16 final_score = 0;

    char * result = tmp;
    str_between(result, buf, "RE[", "]");

    if (result[0] == 0 || strcmp(result, "Void") == 0)
        gr->finished = false;
    else
        if (strcmp(result, "?") == 0 || strcmp(result, "Draw") == 0 ||
            strcmp(result, "0") == 0) {
            finished = true;
        }
        else
            if (result[0] == 'B')
            {
                finished = true;
                if (strlen(result) > 2)
                {
                    result += 2;
                    if (result[0] == 'R')
                    {
                        resignation = true;
                        final_score = 1;
                    }
                    else
                        if (result[0] == 'T')
                        {
                            timeout = true;
                            final_score = 1;
                        }
                        else
                        {
                            double f;
                            if (!parse_float(&f, result))
                            {
                                if (!illegal_final_score_warned) {
                                    illegal_final_score_warned = true;
                                    flog_warn("sgff", "illegal result format");
                                }
                                finished = false;
                                goto after_game_result;
                            }
                            final_score = (d32)(f * 2.0);
                        }
                }
            }
            else
            {
                finished = true;
                if (strlen(result) > 2)
                {
                    result += 2;
                    if (result[0] == 'R')
                    {
                        resignation = true;
                        final_score = -1;
                    }
                    else
                        if (result[0] == 'T')
                        {
                            timeout = true;
                            final_score = -1;
                        }
                        else
                        {
                            double f;
                            if (!parse_float(&f, result))
                            {
                                if (!illegal_final_score_warned) {
                                    illegal_final_score_warned = true;
                                    flog_warn("sgff", "illegal result format");
                                }
                                finished = false;
                                goto after_game_result;
                            }
                            final_score = (d32)(f * -2.0);
                        }
                }
            }

after_game_result:
    release(tmp);

    /*
    Handicap stones
    */
    char * hs = strstr(buf, "AB[");
    if (hs != NULL)
        while (1) {
            hs += 2;
            if (hs[0] == '[' && hs[3] == ']')
            {
                u8 x = hs[1] - 'a';
                u8 y = hs[2] - 'a';
                if (x >= BOARD_SIZ || y >= BOARD_SIZ)
                {
                    if (!illegal_handicap_placement_warned) {
                        illegal_handicap_placement_warned = true;
                        flog_warn("sgff", "handicap placement error (1)");
                    }
                    return false;
                }
                if (!add_handicap_stone(gr, coord_to_move(x, y)))
                {
                    flog_warn("sgff", "handicap placement error (2)");
                    return false;
                }
                hs += 4;
            }
            else
                break;
        }

    /*
    Plays
    */
    char * start = buf;
    char * token;
    char * save_ptr;
    while (gr->turns < MAX_GAME_LENGTH && (token = strtok_r(start, ";)\n\r",
        &save_ptr)) != NULL) {
        if (start != NULL)
            start = NULL;
        size_t token_len = strlen(token);
        if ((token_len >= 3) && (token[0] == 'B' || token[0] == 'W') && token[1]
            == '[') {
            bool is_black = (token[0] == 'B');
            if (token[2] == ']')
                add_play_out_of_order(gr, is_black, PASS);
            else
                if (token_len >= 5 && token[4] == ']')
                {
                    u8 x = token[2] - 'a';
                    u8 y = token[3] - 'a';
                    if (x >= BOARD_SIZ || y >= BOARD_SIZ)
                    {
                        if (!illegal_stone_placement_warned) {
                            illegal_stone_placement_warned = true;
                            flog_warn("sgff", "play coordinate illegal");
                        }
                        return false;
                    }
                    add_play_out_of_order(gr, is_black, coord_to_move(x, y));
                }
        }
    }

    gr->finished = finished;
    gr->resignation = resignation;
    gr->timeout = timeout;
    gr->final_score = final_score;

    return true;
}

/*
RETURNS true if the game has been found and read correctly
*/
bool import_game_from_sgf(
    game_record * gr,
    const char * filename
) {
    char * buf = malloc(MAX_FILE_SIZ);
    if (buf == NULL) {
        flog_crit("sgff", "out of memory");
        return false;
    }

    bool ret = import_game_from_sgf2(gr, filename, buf, MAX_FILE_SIZ);
    free(buf);

    return ret;
}
