/*
Functions related to reading and writing SGF FF[4] files.
http://www.red-bean.com/sgf/

Play variations are not supported.
*/

#include "matilda.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "types.h"
#include "board.h"
#include "file_io.h"
#include "stringm.h"
#include "state_changes.h"
#include "flog.h"
#include "game_record.h"
#include "scoring.h"
#include "buffer.h"

extern s16 komi;

static bool unknown_sgf_ruleset_warning_given = false;
static bool unknown_board_size_cant_guess_warning_given = false;
static bool gm_unknown_warning_given = false;

#define MAX_SGF_FILE_SIZ (8 * 1024)



static u8 guess_board_size(
    const char * sgf
){
    char buf[8];
    for(u8 size = 23; size > 4; size -= 2)
    {
        snprintf(buf, 8, ";B[%c", 'a' + size - 1);
        if(strstr(sgf, buf) != NULL)
            return size;
        snprintf(buf, 8, ";W[%c", 'a' + size - 1);
        if(strstr(sgf, buf) != NULL)
            return size;
    }
    return 0;
}

/*
Reads the header information of an SGF file.
RETURNS true if possibly valid
*/
bool sgf_info(
    const char * sgf_buf,
    bool * black_won,
    bool * chinese_rules,
    bool * japanese_rules,
    bool * normal_komi
){
    *chinese_rules = *japanese_rules = *normal_komi = false;

    if(strstr(sgf_buf, "AB[") != NULL)
        return false;
    if(strstr(sgf_buf, "AL[") != NULL)
        return false;

    /*
    Discover board size
    */
    if(strstr(sgf_buf, "SZ[") == NULL)
    {
        u8 board_size = guess_board_size(sgf_buf);
        if(board_size == 0 && !unknown_board_size_cant_guess_warning_given)
        {
            unknown_board_size_cant_guess_warning_given = true;
            flog_warn("sgf", "board size not specified and could not guess boar\
d size from play coordinates");
        }
        if(board_size != BOARD_SIZ)
            return false;
    }
    else
    {
        char siz_str[8];
        snprintf(siz_str, 8, "SZ[%d]", BOARD_SIZ);
        if(strstr(sgf_buf, siz_str) == NULL)
            return false;
    }

    *chinese_rules = strstr(sgf_buf, "RU[Chinese]") != NULL;
    *japanese_rules = strstr(sgf_buf, "RU[Japanese]") != NULL;
    if(strstr(sgf_buf, "RU[") == NULL)
    {
        *chinese_rules = true;
        *japanese_rules = false;
        if(!unknown_sgf_ruleset_warning_given)
        {
            unknown_sgf_ruleset_warning_given = true;
            flog_warn("sgf", "file doesn't specify ruleset; Chinese rules assum\
ed");
        }
    }

    *normal_komi = (strstr(sgf_buf, "KM[5.") != NULL) || (strstr(sgf_buf,
        "KM[6.") != NULL) || (strstr(sgf_buf, "KM[7.") != NULL);

    if(strstr(sgf_buf, "RE["))
        *black_won = strstr(sgf_buf, "RE[B") != NULL;
    else
        if(strstr(sgf_buf, "Result[Black\\]: "))
            *black_won = strstr(sgf_buf, "Result[Black\\]: B") != NULL;
        else
            return false;

    if(strstr(sgf_buf, ";B[") == NULL)
        return false;
    if(strstr(sgf_buf, ";W[") == NULL)
        return false;

    return true;
}

/*
Reads the sequence of plays from SGF text.
If plays happen out of order they count as a pass by the other player.
RETURNS number of plays found or -1 on format error
*/
s16 sgf_to_boards(
    char * sgf_buf,
    move * plays,
    bool * irregular_play_order
){
    *irregular_play_order = false;
    u16 play = 0;
    board b;
    clear_board(&b);

    char * start = sgf_buf;
    char * token;
    char * save_ptr;
    while(play < MAX_GAME_LENGTH - 1 && (token = strtok_r(start, ";)\n\r",
        &save_ptr)) != NULL)
    {
        if(start != NULL)
            start = NULL;
        size_t token_len = strlen(token);
        if((token_len == 3 || token_len == 5) && (token[0] == 'B' || token[0] ==
            'W') && token[1] == '[')
        {
            bool is_black = (token[0] == 'B');
            if(is_black != ((play & 1) == 0))
            {
                plays[play] = PASS;
                pass(&b);
                *irregular_play_order = true;
            }
            else
                if(token_len == 3 && token[2] == ']')
                {
                    plays[play] = PASS;
                    pass(&b);
                }
                else
                    if(token_len == 5 && token[4] == ']')
                    {
                        u8 i1 = token[2] - 'a';
                        u8 i2 = token[3] - 'a';
                        move m = coord_to_move(i1, i2);
                        plays[play] = m;
                        if(!attempt_play_slow(&b, m, is_black))
                        {
                            flog_warn("gtp","file contains illegal plays");
                            return -1;
                        }
                    }

            ++play;
        }
    }

    return play;
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
){
    char * buf = buffer;
    buf += snprintf(buf, size - (buf - buffer), "(;GM[1]\n");
    buf += snprintf(buf, size - (buf - buffer), "FF[4]\n");
    buf += snprintf(buf, size - (buf - buffer), "SZ[%u]\n", BOARD_SIZ);
    buf += snprintf(buf, size - (buf - buffer), "PW[%s]\n", gr->white_name);
    buf += snprintf(buf, size - (buf - buffer), "PB[%s]\n", gr->black_name);
    buf += snprintf(buf, size - (buf - buffer), "KM[%s]\n",
        komi_to_string(komi));
    if(gr->game_finished)
    {
        if(gr->resignation == 0)
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
    buf += snprintf(buf, size - (buf - buffer), "CA[UTF-8]\n");
    buf += snprintf(buf, size - (buf - buffer), "AP[matilda:%u.%u]\n",
        VERSION_MAJOR, VERSION_MINOR);

    /* Handicap stones */
    if(gr->handicap_stones.count > 1)
    {
        buf += snprintf(buf, size - (buf - buffer), "HA[%u]\nAB",
            gr->handicap_stones.count);
        for(u16 i = 0; i < gr->handicap_stones.count; ++i)
            buf += snprintf(buf, size - (buf - buffer), "[%s]\n",
                coord_to_alpha_alpha(gr->moves[i]));
    }

    /* Plays */
    for(u16 i = 0; i < gr->turns; ++i)
    {
        assert(gr->moves[i] != NONE);
        if(gr->moves[i] == PASS)
        {
            if(gr->handicap_stones.count == 0)
                buf += snprintf(buf, size - (buf - buffer), ";%c[]", (i & 1) ==
                    0 ? 'B' : 'W');
            else
                buf += snprintf(buf, size - (buf - buffer), ";%c[]", (i & 1) ==
                    1 ? 'B' : 'W');
        }
        else
        {
            if(gr->handicap_stones.count == 0)
                buf += snprintf(buf, size - (buf - buffer), ";%c[%s]", (i & 1)
                    == 0 ? 'B' : 'W', coord_to_alpha_alpha(gr->moves[i]));
            else
                buf += snprintf(buf, size - (buf - buffer), ";%c[%s]", (i & 1)
                    == 1 ? 'B' : 'W', coord_to_alpha_alpha(gr->moves[i]));
        }
    }
    buf += snprintf(buf, size - (buf - buffer), ")\n");
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
    char filename[32]
){
    snprintf(filename, 32, "matilda-XXXXXX.sgf");
    int fid = mkstemps(filename, 4);
    if(fid == -1)
        return false;

    char * buffer = (char *)malloc(4 * 1024);
    if(buffer == NULL)
    {
        close(fid);
        return false;
    }

    u32 chars = export_game_as_sgf_to_buffer(gr, buffer, 4 * 1024);
    write(fid, buffer, chars);
    close(fid);

    free(buffer);
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
){
    FILE * fp = fopen(filename, "r");
    if(fp != NULL)
    {
        fprintf(stderr, "file already exists\n");
        fclose(fp);
        return false;
    }
    fp = fopen(filename, "w");
    if(fp == NULL)
        return false;

    char * buffer = (char *)malloc(4 * 1024);
    if(buffer == NULL)
        return false;

    u32 chars = export_game_as_sgf_to_buffer(gr, buffer, 4 * 1024);
    fwrite(buffer, sizeof(char), chars, fp);

    fclose(fp);
    free(buffer);
    return true;
}

/*
RETURNS true if the game has been found and read correctly
*/
bool import_game_from_sgf(
    game_record * gr,
    const char * filename
){
    clear_game_record(gr);

    char * buffer = get_buffer();

    s32 chars_read = read_ascii_file(filename, buffer, MAX_SGF_FILE_SIZ);
    if(chars_read < 1)
    {
        flog_warn("gtp", "could not open/read file");
        return false;
    }

    /*
    Game
    */
    if(strstr(buffer, "GM[1]") == NULL)
        if(!gm_unknown_warning_given)
        {
            gm_unknown_warning_given = true;
            flog_warn("gtp", "SGF GM[1] annotation not found");
        }

    /*
    Board size
    */
    char * board_size_str = str_between(buffer, "SZ[", "]");
    if(board_size_str == NULL)
    {
        u8 board_size = guess_board_size(buffer);
        if(board_size == 0 && !unknown_board_size_cant_guess_warning_given)
        {
            unknown_board_size_cant_guess_warning_given = true;
            flog_warn("gtp", "board size not specified and could not be guessed\
 from play coordinates");
        }
        if(board_size != BOARD_SIZ)
        {
            flog_warn("gtp", "wrong board size");
            return false;
        }
    }
    else
        if(!strcmp(board_size_str, BOARD_SIZ_AS_STR))
        {
            flog_warn("gtp", "illegal board size format");
            return false;
        }

    /*
    Player names
    */
    char * black_name = str_between(buffer, "PB[", "]");
    if(black_name != NULL)
        strncpy(gr->black_name, black_name, 16);

    char * white_name = str_between(buffer, "PW[", "]");
    if(white_name != NULL)
        strncpy(gr->white_name, white_name, 16);


    /*
    Result
    */
    char * result = str_between(buffer, "RE[", "]");

    if(result == NULL || strcmp(result, "Void") == 0)
        gr->game_finished = false;
    else
        if(strcmp(result, "?") == 0 || strcmp(result, "Draw") == 0 ||
            strcmp(result, "0") == 0)
        {
            gr->game_finished = true;
            gr->resignation = false;
            gr->final_score = 0;
        }
        else
            if(result[0] == 'B')
            {
                gr->game_finished = true;
                if(strlen(result) > 2)
                {
                    result += 2;
                    if(result[0] == 'R')
                    {
                        gr->resignation = true;
                        gr->final_score = 1;
                    }
                    else
                    {
                        gr->resignation = false;
                        parse_int(result, (s32 *)&gr->final_score);
                        gr->final_score *= 2;
                        if(gr->final_score == 0)
                        {
                            flog_warn("gtp", "illegal final score");
                            return false;
                        }
                        if(strstr(result, ".") != NULL)
                            gr->final_score++;
                    }
                }
            }
            else
            {
                gr->game_finished = true;
                if(strlen(result) > 2)
                {
                    result += 2;
                    if(result[0] == 'R')
                    {
                        gr->resignation = true;
                        gr->final_score = -1;
                    }
                    else
                    {
                        gr->resignation = false;
                        parse_int(result, (s32 *)&gr->final_score);
                        gr->final_score *= -2;
                        if(gr->final_score == 0)
                        {
                            flog_warn("gtp", "illegal final score");
                            return false;
                        }
                        if(strstr(result, ".") != NULL)
                            gr->final_score--;
                    }
                }
            }

    /*
    Handicap stones
    */
    char * hs = strstr(buffer, "AB[");
    if(hs != NULL)
        while(1)
        {
            hs += 2;
            if(hs[0] == '[' && hs[3] == ']')
            {
                u8 x = hs[1] - 'a';
                u8 y = hs[2] - 'a';
                if(!add_handicap_stone(gr, coord_to_move(x, y)))
                {
                    flog_warn("gtp", "handicap placement error");
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
    char * start = buffer;
    char * token;
    char * save_ptr;
    while(gr->turns < MAX_GAME_LENGTH && (token = strtok_r(start, ";)\n\r",
        &save_ptr)) != NULL)
    {
        if(start != NULL)
            start = NULL;
        size_t token_len = strlen(token);
        if((token_len >= 3) && (token[0] == 'B' || token[0] == 'W') && token[1]
            == '[')
        {
            bool is_black = (token[0] == 'B');
            if(token[2] == ']')
                add_play_out_of_order(gr, is_black, PASS);
            else
                if(token_len >= 5 && token[4] == ']')
                {
                    u8 x = token[2] - 'a';
                    u8 y = token[3] - 'a';
                    add_play_out_of_order(gr, is_black, coord_to_move(x, y));
                }
        }
    }

    return true;
}

