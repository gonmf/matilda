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
#include "pts_file.h"
#include "state_changes.h"
#include "stringm.h"
#include "types.h"

#define NR_BUCKETS 769

static ob_entry ** ob_trans_table;
static bool attempted_discover_ob = false;
static u32 ob_rules = 0;

static move ob_get_play(
    u32 hash,
    const u8 p[PACKED_BOARD_SIZ]
){
    ob_entry * h = ob_trans_table[hash % NR_BUCKETS];
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
    e->next = ob_trans_table[e->hash % NR_BUCKETS];
    ob_trans_table[e->hash % NR_BUCKETS] = e;
}

static bool process_opening_book_line(
    char * s
){
    char * save_ptr = NULL;
    char * word;

    u16 tokens_read = 0;
    char tokens[TOTAL_BOARD_SIZ][4];

    if((word = strtok_r(s, " ", &save_ptr)) != NULL)
        strncpy(tokens[tokens_read++], word, 4);

    while(tokens_read < TOTAL_BOARD_SIZ && (word = strtok_r(NULL, " ",
        &save_ptr)) != NULL)
        strncpy(tokens[tokens_read++], word, 4);

    if(tokens_read < 2 || tokens_read == TOTAL_BOARD_SIZ)
        flog_crit("ob", "illegal opening book rule");

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
            flog_crit("ob", "illegal opening book rule");

        if(!attempt_play_slow(&b, is_black, m))
            flog_crit("ob", "illegal opening book rule");

        is_black = !is_black;
    }

    b.last_played = b.last_eaten = NONE;

    move m = coord_parse_alpha_num(tokens[t + 1]);
    if(!is_board_move(m))
        flog_crit("ob", "illegal opening book rule");

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
void discover_opening_books()
{
    if(attempted_discover_ob)
        return;

    attempted_discover_ob = true;
    ob_rules = 0;

    /*
    Allocate O.B. hash table
    */
    ob_trans_table = (ob_entry **)calloc(NR_BUCKETS, sizeof(ob_entry *));
    if(ob_trans_table == NULL)
        flog_crit("ob", "system out of memory");
    char * filenames[32];
    u32 files_found;

    char * s = alloc();
    char * l = alloc();

    /*
    Discover .ob files
    */
    files_found = recurse_find_files(get_data_folder(), ".ob", filenames, 32);
    snprintf(s, MAX_PAGE_SIZ, "found %u opening book files\n", files_found);
    flog_info("ob", s);

    for(u32 i = 0; i < files_found; ++i){
        open_rule_file(filenames[i]);
        u32 rules_saved = 0;
        u32 rules_found = 0;
        while(1){
            read_next_rule(l);
            if(l[0] == 0)
                break;
            if(process_opening_book_line(l))
                ++rules_saved;
            ++rules_found;
        }
        close_rule_file();
        ob_rules += rules_saved;

        snprintf(s, MAX_PAGE_SIZ, "read %s (%u/%u rules)\n", filenames[i],
            rules_saved, rules_found);
        flog_info("ob", s);

        free(filenames[i]);
    }

    release(l);
    release(s);
}

/*
Match an opening rule and return it encoded in the board.
RETURNS true if rule found
*/
bool opening_book(
    out_board * out_b,
    board * state
){
    discover_opening_books();

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

