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

#include "board.h"
#include "flog.h"
#include "crc32.h"
#include "timem.h"
#include "opening_book.h"
#include "stringm.h"
#include "file_io.h"
#include "state_changes.h"
#include "types.h"
#include "pts_file.h"
#include "engine.h"
#include "alloc.h"

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
        return false;

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
            return false;
        if(!attempt_play_slow(&b, m, is_black))
            return false;

        is_black = !is_black;
    }

    move m = coord_parse_alpha_num(tokens[t + 1]);
    if(!is_board_move(m))
        return false;

    d8 reduction = reduce_auto(&b, is_black);
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

static bool process_state_play_line(
    char * s
){
    char * save_ptr = NULL;
    char * word = strtok_r(s, " ", &save_ptr);
    if(word == NULL)
        return false;

    board b;
    for(move m = 0; m < TOTAL_BOARD_SIZ; ++m)
    {
        if(word[m] == EMPTY_STONE_CHAR)
            b.p[m] = EMPTY;
        else
            if(word[m] == BLACK_STONE_CHAR)
                b.p[m] = BLACK_STONE;
            else
                if(word[m] == WHITE_STONE_CHAR)
                    b.p[m] = WHITE_STONE;
                else
                    return false;
    }
    b.last_played = b.last_eaten = NONE;

    word = strtok_r(NULL, " ", &save_ptr);
    if(word == NULL)
        return false;

    move play = coord_parse_alpha_num(word);

    d8 reduction = reduce_auto(&b, true);
    play = reduce_move(play, reduction);

    if(!is_board_move(play))
        return false;

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
    obe->play = play;
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

    /*
    Allocate O.B. hash table
    */
    ob_trans_table = (ob_entry **)calloc(NR_BUCKETS, sizeof(ob_entry *));
    if(ob_trans_table == NULL)
        flog_crit("ob", "system out of memory");

    /*
    Discover .ob files
    */
    char * filenames[32];
    u32 files_found = recurse_find_files(get_data_folder(), ".ob", filenames,
        32);

    char * s = alloc();
    snprintf(s, MAX_PAGE_SIZ, "found %u opening book files\n", files_found);
    flog_info("ob", s);

    char * l = alloc();

    for(u32 i = 0; i < files_found; ++i){
        open_rule_file(filenames[i]);
        u32 rules_found = 0;
        while(1){
            read_next_rule(l);
            if(l[0] == 0)
                break;

            if(process_opening_book_line(l))
                ++rules_found;
        }
        close_rule_file();
        ob_rules += rules_found;

        snprintf(s, MAX_PAGE_SIZ, "read %s (%u rules)\n", filenames[i],
            rules_found);
        flog_info("ob", s);

        free(filenames[i]);
    }

    /*
    Discover .spb files
    */
    files_found = recurse_find_files(get_data_folder(), ".spb", filenames, 32);

    snprintf(s, MAX_PAGE_SIZ, "found %u state,play files\n", files_found);
    flog_info("spb", s);

    for(u32 i = 0; i < files_found; ++i)
    {
        open_rule_file(filenames[i]);
        u32 rules_found = 0;
        while(1){
            read_next_rule(l);
            if(l[0] == 0)
                break;

            if(process_state_play_line(l))
                ++rules_found;
        }
        close_rule_file();

        snprintf(s, MAX_PAGE_SIZ, "read %s (%u rules)\n", filenames[i],
            rules_found);
        flog_info("spb", s);

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
    if(test_ko(state, m, BLACK_STONE))
        return false;

    flog_info("ob", "transition rule found");

    out_b->tested[m] = true;
    out_b->value[m] = 1.0;
    return true;
}

