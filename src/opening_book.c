/*
Strategy that makes use of an opening book.
*/

#include "matilda.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "alloc.h"
#include "board.h"
#include "crc32.h"
#include "engine.h"
#include "file_io.h"
#include "flog.h"
#include "opening_book.h"
#include "primes.h"
#include "pts_file.h"
#include "state_changes.h"
#include "stringm.h"
#include "types.h"

static ob_entry ** ob_trans_table;
static bool attempted_discover_ob = false;
static u32 ob_rules = 0;
static u32 nr_buckets = 0;

#define MAX_RULE_TOKENS (TOTAL_BOARD_SIZ + TOTAL_BOARD_SIZ / 2)

static move ob_get_play(
    u32 hash,
    const u8 p[PACKED_BOARD_SIZ]
){
    ob_entry * h = ob_trans_table[hash % nr_buckets];
    while(h != NULL)
    {
        if(h->hash == hash && memcmp(&h->p, p, PACKED_BOARD_SIZ) == 0)
            return h->play;
        h = h->next;
    }
    return NONE;
}

/*
Formats a board position to a Fuego-style opening book rule, for example:
13 K4 C3 | F11
With no line break. Does not ascertain the validity of the rule, i.e. do not
invoke after a capture or pass has occurred.
// TODO add assertions that format is well understood
*/
void board_to_ob_rule(
    char * dst,
    u8 p[TOTAL_BOARD_SIZ],
    move play
){
    u32 idx = snprintf(dst, MAX_PAGE_SIZ, "%u ", BOARD_SIZ);
    move m1 = 0;
    move m2 = 0;
    bool is_black = true;
    char * mstr = alloc();

    bool found;
    do
    {
        found = false;
        if(is_black)
        {
            for(; m1 < TOTAL_BOARD_SIZ; ++m1)
                if(p[m1] == BLACK_STONE)
                {
                    coord_to_alpha_num(mstr, m1);
                    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "%s ", mstr);
                    found = true;
                    ++m1;
                    break;
                }
        }
        else
        {
            for(; m2 < TOTAL_BOARD_SIZ; ++m2)
                if(p[m2] == WHITE_STONE)
                {
                    coord_to_alpha_num(mstr, m2);
                    idx += snprintf(dst + idx, MAX_PAGE_SIZ - idx, "%s ", mstr);
                    found = true;
                    ++m2;
                    break;
                }
        }
        is_black = !is_black;
    }while(found);

    coord_to_alpha_num(mstr, play);
    snprintf(dst + idx, MAX_PAGE_SIZ - idx, "| %s\n", mstr);
    release(mstr);
}

static void ob_simple_insert(
    ob_entry * e
){
    e->next = ob_trans_table[e->hash % nr_buckets];
    ob_trans_table[e->hash % nr_buckets] = e;
}

static bool process_opening_book_line(
    char * s
){
    char * saveptr = NULL;
    char * word;

    if((word = strtok_r(s, " ", &saveptr)) == NULL)
        return false;

    if(strcmp(word, BOARD_SIZ_AS_STR) != 0)
        return false;

    u16 tokens_read = 0;
    char tokens[MAX_RULE_TOKENS][4];

    while(tokens_read < MAX_RULE_TOKENS && (word = strtok_r(NULL, " ",
        &saveptr)) != NULL)
    {
        if(word[0] == '#')
            break;
        strncpy(tokens[tokens_read++], word, 4);
    }

    if(tokens_read < 2 || tokens_read == MAX_RULE_TOKENS)
    {
        flog_crit("ob", "illegal opening book rule: size");
    }

    board b;
    clear_board(&b);
    bool is_black = true;

    u16 t = 0;
    for(; t < tokens_read - 2; ++t)
    {
        if(strcmp(tokens[t], "|") == 0)
            break;
        move m = coord_parse_alpha_num(tokens[t]);
        if(!is_board_move(m))
            flog_crit("ob", "illegal opening book rule: play string format");

        if(!attempt_play_slow(&b, is_black, m))
            flog_crit("ob", "illegal opening book rule: play sequence");

        is_black = !is_black;
    }

    b.last_played = b.last_eaten = NONE;

    move m = coord_parse_alpha_num(tokens[t + 1]);
    if(!is_board_move(m))
        flog_crit("ob", "illegal opening book rule: response play");

    d8 reduction = reduce_auto(&b, true);
    m = reduce_move(m, reduction);

    u8 packed_board[PACKED_BOARD_SIZ];
    pack_matrix(packed_board, b.p);
    u32 hash = crc32(packed_board, PACKED_BOARD_SIZ);

    move mt = ob_get_play(hash, packed_board);
    if(mt != NONE)
        return false;

    ob_entry * obe = malloc(sizeof(ob_entry));
    if(obe == NULL)
        flog_crit("ob", "system out of memory");

    obe->hash = hash;
    memcpy(obe->p, packed_board, PACKED_BOARD_SIZ);
    obe->play = m;
    ob_simple_insert(obe);
    return true;
}

/*
Discover and read opening book files.
*/
void opening_book_init()
{
    if(attempted_discover_ob)
        return;

    attempted_discover_ob = true;

    nr_buckets = get_prime_near(BOARD_SIZ * BOARD_SIZ * BOARD_SIZ * 2);

    /*
    Allocate O.B. hash table
    */
    ob_trans_table = (ob_entry **)calloc(nr_buckets, sizeof(ob_entry *));
    if(ob_trans_table == NULL)
        flog_crit("ob", "system out of memory");

    /*
    Read .ob file
    */
    char * filename = alloc();
    snprintf(filename, MAX_PAGE_SIZ, "%s%ux%u.ob", get_data_folder(), BOARD_SIZ, 
        BOARD_SIZ);

    char * buffer = malloc(MAX_FILE_SIZ);
    if(buffer == NULL)
        flog_crit("ob", "system out of memory");

    d32 chars_read = read_ascii_file(buffer, MAX_FILE_SIZ, filename);
    if(chars_read < 0)
    {
        char * s = alloc();
        snprintf(s, MAX_PAGE_SIZ, "could not read %s", filename);
        flog_warn("ob", s);
        release(s);
        release(filename);
        return;
    }

    u32 rules_saved = 0;
    u32 rules_found = 0;
    char * saveptr = NULL;
    char * tmp = buffer;
    char * line;
    while((line = strtok_r(tmp, "\r\n", &saveptr)) != NULL)
    {
        tmp = NULL;

        if(process_opening_book_line(line))
            ++rules_saved;
        ++rules_found;
    }

    ob_rules = rules_saved;

    char * s = alloc();
    snprintf(s, MAX_PAGE_SIZ, "read %s (%u/%u rules)", filename, rules_saved, rules_found);
    flog_info("ob", s);
    release(s);
    release(filename);
    free(buffer);
}

/*
Match an opening rule and return it encoded in the board.
RETURNS true if rule found
*/
bool opening_book(
    out_board * out_b,
    board * state
){
    opening_book_init();

    clear_out_board(out_b);

    if(ob_rules == 0)
        return false;
    if(state->last_eaten != NONE)
        return false;

    u8 packed_board[PACKED_BOARD_SIZ];
    pack_matrix(packed_board, state->p);
    u32 hash = crc32(packed_board, PACKED_BOARD_SIZ);

    move m = ob_get_play(hash, packed_board);
    if(m == NONE)
        return false;

    /*
    Since the O.B. do not include last eaten information, they may
    suggest a play that is illegal by ko. Prevent this.
    */
    if(is_board_move(state->last_played) &&
        test_ko(state, m, state->p[state->last_played]))
        return false;

    flog_info("ob", "transition rule found");

    out_b->tested[m] = true;
    out_b->value[m] = 1.0;
    return true;
}

