/*
Functions that support the use of small 3x3 patterns hand crafted by the authors
of GNU Go, MoGo and others over the years.

The life of these patterns is as follow:
 * On startup a pat3 file is loaded with a number of 3x3 patterns suggesting
 play at the center intersection. The pattern is flipped and rotated and stored
 in a hash table for both players (with the color inverted for white). They are
 stored in their 16-bit value form.

 * In MCTS each candidate position can be transposed to a 3x3 array, which fixed
 out of bounds codification, fliped and rotated (but the color remains the same)
 and searched for in the appropriate hash table.
*/

#include "matilda.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "alloc.h"
#include "board.h"
#include "engine.h"
#include "file_io.h"
#include "flog.h"
#include "hash_table.h"
#include "matrix.h"
#include "pat3.h"
#include "stringm.h"
#include "types.h"


static u16 b_pattern_table[65536];
static u16 w_pattern_table[65536];
static bool pat3_table_inited = false;

static hash_table * weights_table = NULL;
static u32 weights_found = 0;
static u32 weights_not_found = 0;


typedef struct __pat3_{
    u16 value;
    u16 weight;
} pat3;

/*
Convert some final symbols; non-final symbols are left as-is here
*/
static void clean_symbols(
    u8 p[3][3]
){
    char * s;
    for(u8 i = 0; i < 3; ++i)
        for(u8 j = 0; j < 3; ++j)
            switch(p[i][j])
            {
                case SYMBOL_EMPTY:
                    p[i][j] = EMPTY;
                    break;
                case SYMBOL_BORDER:
                    p[i][j] = ILLEGAL;
                    break;
                case SYMBOL_OWN_STONE:
                    p[i][j] = BLACK_STONE;
                    break;
                case SYMBOL_OPT_STONE:
                    p[i][j] = WHITE_STONE;
                    break;
                case SYMBOL_OWN_OR_EMPTY:
                case SYMBOL_OPT_OR_EMPTY:
                case SYMBOL_STONE_OR_EMPTY:
                    break;
                default:
                    s = alloc();
                    snprintf(s, MAX_PAGE_SIZ,
                        "pattern file format error; unknown symbol: '%c', %u\n",
                        p[i][j], p[i][j]);
                    flog_crit("pat3", s);
                    release(s);
            }
}

static void pat3_insert(
    u16 value,
    u16 value_inv,
    u16 weight
){
    /* patterns from blacks perspective */
    b_pattern_table[value] = weight;

    /* patterns from whites perspective */
    w_pattern_table[value_inv] = weight;
}

/*
Lookup of pattern value for the specified player.
RETURNS pattern weight or 0 if not found
*/
u16 pat3_find(
    u16 value,
    bool is_black
){
    return is_black ? b_pattern_table[value] : w_pattern_table[value];
}

static void flip(
    const u8 src[3][3],
    u8 dst[3][3]
){
    for(u8 i = 0; i < 3; ++i)
        for(u8 j = 0; j < 3; ++j)
            dst[i][j] = src[2 - i][j];
}

static void rotate(
    const u8 src[3][3],
    u8 dst[3][3],
    u8 rotations
){
    u8 i;
    u8 j;
    switch(rotations)
    {
        case 1:
            for(i = 0; i < 3; ++i)
                for(j = 0; j < 3; ++j)
                    dst[i][j] = src[2 - j][i];
            break;
        case 2:
            for(i = 0; i < 3; ++i)
                for(j = 0; j < 3; ++j)
                    dst[i][j] = src[2 - i][2 - j];
            break;
        case 3:
            for(i = 0; i < 3; ++i)
                for(j = 0; j < 3; ++j)
                    dst[i][j] = src[j][2 - i];
            break;
    }
}

static void reduce_pattern(
    u8 v[3][3],
    u8 method
){
    if(method == NOREDUCE)
        return;

    u8 r[3][3];
    u8 f[3][3];
    switch(method)
    {
        case ROTATE90:
            rotate((const u8 (*)[3])v, r, 1);
            break;
        case ROTATE180:
            rotate((const u8 (*)[3])v, r, 2);
            break;
        case ROTATE270:
            rotate((const u8 (*)[3])v, r, 3);
            break;
        case ROTFLIP0:
            flip((const u8 (*)[3])v, r);
            break;
        case ROTFLIP90:
            rotate((const u8 (*)[3])v, f, 1);
            flip((const u8 (*)[3])f, r);
            break;
        case ROTFLIP180:
            rotate((const u8 (*)[3])v, f, 2);
            flip((const u8 (*)[3])f, r);
            break;
        case ROTFLIP270:
            rotate((const u8 (*)[3])v, f, 3);
            flip((const u8 (*)[3])f, r);
            break;
        default:
            return;
    }
    memcpy(v, r, 3 * 3);
}

/*
Rotate and flip the pattern to its unique representative.
Avoid using, is not optimized.
*/
void pat3_reduce_auto(
    u8 v[3][3]
){
    u8 b[3][3];

    for(u8 reduction = ROTATE90; reduction <= ROTFLIP270; ++reduction)
    {
        memcpy(b, v, 3 * 3);
        reduce_pattern(b, ROTATE90);
        if(memcmp(b, v, 3 * 3) < 0)
            memcpy(v, b, 3 * 3);
    }
}

/*
Transposes part of an input matrix board into a 3x3 matrix pattern codified,
with board safety.
*/
void pat3_transpose(
    u8 dst[3][3],
    const u8 p[TOTAL_BOARD_SIZ],
    move m
){
    assert(is_board_move(m));
    assert(p[m] == EMPTY);
    d8 x;
    d8 y;
    move_to_coord(m, (u8 *)&x, (u8 *)&y);
    d8 i;
    d8 j;
    d8 ki;
    d8 kj;
    for(j = y - 1, kj = 0; j <= y + 1; ++j, ++kj)
        for(i = x - 1, ki = 0; i <= x + 1; ++i, ++ki)
            if(i >= 0 && j >= 0 && i < BOARD_SIZ && j < BOARD_SIZ)
            {
                move n = coord_to_move(i, j);
                dst[ki][kj] = p[n];
            }else
                dst[ki][kj] = ILLEGAL; /* edge of the board */
}

/*
Codifies the pattern in a 16 bit unsigned value.
*/
u16 pat3_to_string(
    const u8 p[3][3]
){
    u16 ret = p[0][0] & 3;
    ret = (ret << 2) + (p[0][1] & 3);
    ret = (ret << 2) + (p[0][2] & 3);

    ret = (ret << 2) + (p[1][0] & 3);
    assert(p[1][1] == EMPTY);
    ret = (ret << 2) + (p[1][2] & 3);

    ret = (ret << 2) + (p[2][0] & 3);
    ret = (ret << 2) + (p[2][1] & 3);
    ret = (ret << 2) + (p[2][2] & 3);
    return ret;
}

/*
Decodes a 16-bit value into a 3x3 pattern, with empty center.
*/
void string_to_pat3(
    u8 dst[3][3],
    u16 src
){
    dst[2][2] = src & 3;
    src >>= 2;
    dst[2][1] = src & 3;
    src >>= 2;
    dst[2][0] = src & 3;
    src >>= 2;
    dst[1][2] = src & 3;
    dst[1][1] = EMPTY;
    src >>= 2;
    dst[1][0] = src & 3;
    src >>= 2;
    dst[0][2] = src & 3;
    src >>= 2;
    dst[0][1] = src & 3;
    src >>= 2;
    dst[0][0] = src & 3;
}

static u8 _count_stones(
    const u8 p[3][3]
){
    u8 ret = 0;
    for(u8 i = 0; i < 3; ++i)
        for(u8 j = 0; j < 3; ++j)
            if(p[i][j] == WHITE_STONE || p[i][j] == BLACK_STONE)
                ++ret;
    return ret;
}

/*
Invert stone colors.
*/
void pat3_invert(
    u8 p[3][3]
){
    for(u8 i = 0; i < 3; ++i)
        for(u8 j = 0; j < 3; ++j)
            if(p[i][j] == BLACK_STONE)
                p[i][j] = WHITE_STONE;
            else
                if(p[i][j] == WHITE_STONE)
                    p[i][j] = BLACK_STONE;
}

static void multiply_and_store(
    const u8 pat[3][3]
){
    u8 p[3][3];
    u16 weight = 1;

    if(weights_table != NULL)
    {
        /* Discover weight */
        memcpy(p, pat, 3 * 3);
        pat3_reduce_auto(p);
        u16 pattern = pat3_to_string((const u8 (*)[3])p);
        pat3 tmp;
        tmp.value = pattern;
        pat3 * tmp2 = (pat3 *)hash_table_find(weights_table, &tmp);
        if(tmp2 == NULL)
        {
            weight = (65535 / WEIGHT_SCALE);
            weights_not_found++;
        }
        else
        {
            weight = tmp2->weight;
            weights_found++;
        }
    }

    u8 p_inv[3][3];
    for(u8 r = 1; r < 9; ++r)
    {
        memcpy(p, pat, 3 * 3);
        reduce_pattern(p, r);
        u16 value = pat3_to_string((const u8 (*)[3])p);
        if(pat3_find(value, true) == 0)
        {
            memcpy(p_inv, p, 3 * 3);
            pat3_invert(p_inv);
            u16 value_inv = pat3_to_string((const u8 (*)[3])p);
            assert(pat3_find(value_inv, false) == 0);
            pat3_insert(value, value_inv, weight);
        }
    }
}

/*
The original pattern is expanded into all possible forms, then rotated/flip and
saved if unique in the hash table under the correct patterns group (patterns
generated from same original p.). For uniqueness the attributes are also taken
into consideration.

RETURNS total number of new unique patterns
*/
static void expand_pattern(
    const u8 pat[3][3]
){
    u8 p[3][3];
    memcpy(p, pat, 3 * 3);

    for(u8 i = 0; i < 3; ++i)
        for(u8 j = 0; j < 3; ++j)
            switch(p[i][j])
            {
                case SYMBOL_OWN_OR_EMPTY:
                    p[i][j] = BLACK_STONE;
                    expand_pattern((const u8 (*)[3])p);
                    p[i][j] = EMPTY;
                    expand_pattern((const u8 (*)[3])p);
                    return;
                case SYMBOL_OPT_OR_EMPTY:
                    p[i][j] = WHITE_STONE;
                    expand_pattern((const u8 (*)[3])p);
                    p[i][j] = EMPTY;
                    expand_pattern((const u8 (*)[3])p);
                    return;
                case SYMBOL_STONE_OR_EMPTY:
                    p[i][j] = BLACK_STONE;
                    expand_pattern((const u8 (*)[3])p);
                    p[i][j] = WHITE_STONE;
                    expand_pattern((const u8 (*)[3])p);
                    p[i][j] = EMPTY;
                    expand_pattern((const u8 (*)[3])p);
                    return;
            }
    if(_count_stones((const u8 (*)[3])p) < 2)
        flog_crit("pat3", "failed to open and expand patterns because the expan\
sion would create patterns with a single stone or less");

    /* invert color, rotate and flip to generate equivalent pattern
    configurations */
    multiply_and_store((const u8 (*)[3])p);
}


static u32 read_pat3_file(
    const char * filename,
    char * buffer
){
    d32 chars_read = read_ascii_file(buffer, MAX_FILE_SIZ, filename);
    if(chars_read < 0)
        flog_crit("pat3", "couldn't open file for reading");

    u8 pat[3][3];
    u8 pat_pos = 0;
    u32 pats_read = 0;

    char * line;
    char * init_str = buffer;
    char * save_ptr;
    while((line = strtok_r(init_str, "\r\n", &save_ptr)) != NULL)
    {
        init_str = NULL;

        line_cut_before(line, '#');
        line = trim(line);
        if(line == NULL)
            continue;
        u16 len = strlen(line);
        if(len == 0)
            continue;

        if(len == 3){
            pat[pat_pos][0] = line[0];
            pat[pat_pos][1] = line[1];
            pat[pat_pos][2] = line[2];
            ++pat_pos;

            if(pat_pos == 3){
                clean_symbols(pat);

                /* generate all combinations and store in hash tables */
                expand_pattern((const u8 (*)[3])pat);
                pats_read += 1;

                pat_pos = 0;
            }
        }
    }

    return pats_read;
}

static u32 pat3_hash_function(
    void * a
){
    pat3 * b = (pat3 *)a;
    return b->value;
}

static int pat3_compare_function(
    const void * a,
    const void * b
){
    pat3 * f1 = (pat3 *)a;
    pat3 * f2 = (pat3 *)b;
    return ((d32)(f2->value)) - ((d32)(f1->value));
}

static u32 read_patern_weights(
    char * buffer
){
    weights_table = hash_table_create(1543, sizeof(pat3), pat3_hash_function,
        pat3_compare_function);

    char * line;
    char * init_str = buffer;
    char * save_ptr;
    while((line = strtok_r(init_str, "\r\n", &save_ptr)) != NULL)
    {
        init_str = NULL;

        line_cut_before(line, '#');
        line = trim(line);
        if(line == NULL)
            continue;
        u16 len = strlen(line);
        if(len == 0)
            continue;

        char * save_ptr2;
        char * word1 = strtok_r(line, " ", &save_ptr2);
        if(word1 == NULL)
            continue;
        char * word2 = strtok_r(NULL, " ", &save_ptr2);
        if(word2 == NULL)
            continue;

        long int tmp1 = strtol(word1, NULL, 16);
        long int tmp2 = strtol(word2, NULL, 10);
        if(tmp1 > 65535 || tmp2 > 65535)
            continue;

        u16 pattern = (u16)tmp1;
        /* Weight scaling for totals to fit in 16 bit and not having 0s */
        tmp2 = (tmp2 / WEIGHT_SCALE) + 1;
        u16 weight = (u16)tmp2;

        pat3 * p = malloc(sizeof(pat3));
        p->value = pattern;
        p->weight = weight;
        if(!hash_table_exists(weights_table, p))
            hash_table_insert_unique(weights_table, p);
    }

    return weights_table->elements;
}

/*
Reads a .pat3 patterns file and expands all patterns into all possible and
patternable configurations.
*/
void pat3_init()
{
    if(pat3_table_inited)
        return;
    pat3_table_inited = true;

    char * file_buf = (char *)malloc(MAX_FILE_SIZ);
    if(file_buf == NULL)
        flog_crit("pat3", "system out of memory");

    char * buf = alloc();

    if(USE_PATTERN_WEIGHTS)
    {
        /*
        Read pattern weights file
        */
        char * filename = alloc();
        snprintf(filename, MAX_PAGE_SIZ, "%s%ux%u.weights", data_folder(),
            BOARD_SIZ, BOARD_SIZ);

        d32 chars_read = read_ascii_file(file_buf, MAX_FILE_SIZ, filename);
        if(chars_read < 0)
        {
            char * s = alloc();
            snprintf(s, MAX_PAGE_SIZ, "could not read %s", filename);
            flog_warn("pat3", s);
            release(s);
        }
        else
        {
            u32 weights = read_patern_weights(file_buf);

            snprintf(buf, MAX_PAGE_SIZ, "read %s (%u weights)", filename,
                weights);
            flog_info("pat3", buf);
        }
        release(filename);
    }

    /*
    Discover .pat3 files
    */
    char * pat3_filenames[128];
    u32 files_found = recurse_find_files(data_folder(), ".pat3",
        pat3_filenames, 128);

    snprintf(buf, MAX_PAGE_SIZ, "found %u 3x3 pattern files", files_found);
    flog_info("pat3", buf);

    for(u32 i = 0; i < files_found; ++i)
    {
        u32 patterns_found = read_pat3_file(pat3_filenames[i], file_buf);

        snprintf(buf, MAX_PAGE_SIZ, "read %s (%u patterns)", pat3_filenames[i], patterns_found);
        flog_info("pat3", buf);

        free(pat3_filenames[i]);
    }

    free(file_buf);

    if(USE_PATTERN_WEIGHTS && weights_table != NULL)
    {
        snprintf(buf, MAX_PAGE_SIZ, "%u/%u expanded patterns weighted", weights_found,
            weights_found + weights_not_found);
        flog_info("pat3", buf);

        hash_table_destroy(weights_table, true);
        weights_table = NULL;
    }

    release(buf);
}


