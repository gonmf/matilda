/*
Program for the evaluation of positions from game records and the calculation of
opening book rules. All positions before a pass or capture are extracted from
the game record files, sorted by number of occurrences, and MCTS is used to
determine the best response play.

It reads .sgf files in the data directory and produces a unique .ob file in the
same directory.
*/


#include "matilda.h"

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "board.h"
#include "constants.h"
#include "crc32.h"
#include "engine.h"
#include "file_io.h"
#include "flog.h"
#include "hash_table.h"
#include "mcts.h"
#include "opening_book.h"
#include "randg.h"
#include "sgf.h"
#include "state_changes.h"
#include "stringm.h"
#include "timem.h"
#include "transpositions.h"
#include "zobrist.h"



#define MAX_FILES 500000
#define TABLE_BUCKETS 4957


static char * filenames[MAX_FILES];

static u32 secs_per_turn = 60;
static d32 ob_depth = TOTAL_BOARD_SIZ / 2;

typedef struct __simple_state_transition_ {
    u8 p[PACKED_BOARD_SIZ];
    u32 popularity;
    u32 hash;
} simple_state_transition;


static u32 hash_function(
    void * o
){
    simple_state_transition * s = (simple_state_transition *)o;
    return s->hash;
}

static int compare_function(
    const void * o1,
    const void * o2
){
    simple_state_transition * s1 = (simple_state_transition *)o1;
    simple_state_transition * s2 = (simple_state_transition *)o2;
    return memcmp(s1->p, s2->p, PACKED_BOARD_SIZ);
}

static int sort_cmp_function(
    const void * o1,
    const void * o2
){
    simple_state_transition ** s1 = (simple_state_transition **)o1;
    simple_state_transition ** s2 = (simple_state_transition **)o2;
    return ((int)(*s2)->popularity) - ((int)(*s1)->popularity);
}


int main(int argc, char * argv[]){
    bool no_print = false;

    for(int i = 1; i < argc; ++i){
        if(i < argc - 1 && strcmp(argv[i], "--time") == 0){
            d32 a;
            if(!parse_int(&a, argv[i + 1]) || a < 1)
                goto lbl_usage;
            ++i;
            secs_per_turn = a;
            continue;
        }
        if(strcmp(argv[i], "--no_print") == 0){
            no_print = true;
            continue;
        }
        if(i < argc - 1 && strcmp(argv[i], "--max_depth") == 0){
            d32 a;
            if(!parse_int(&a, argv[i + 1]) || a < 1)
                goto lbl_usage;
            ++i;
            ob_depth = a;
            continue;
        }

lbl_usage:
        printf("Usage: %s [options]\n", argv[0]);
        printf("Options:\n");
        printf("--max_depth number - Maximum turn depth of the openings. (defau\
lt: %u)\n", ob_depth);
        printf("--no_print - Do not print SGF filenames.\n");
        printf("--time number - Time spent per rule, in seconds. (default: %u)\\
n", secs_per_turn);
        exit(EXIT_SUCCESS);
    }

    alloc_init();

    flog_config_modes(LOG_MODE_ERROR | LOG_MODE_WARN);
    flog_config_destinations(LOG_DEST_STDF);

    rand_init();
    assert_data_folder_exists();
    board_constants_init();
    zobrist_init();
    tt_init();

    char * str = alloc();
    char * ts = alloc();
    timestamp(ts);
    printf("%s: Creating table...\n", ts);
    hash_table * table = hash_table_create(TABLE_BUCKETS,
        sizeof(simple_state_transition), hash_function, compare_function);

    u32 games_used = 0;
    u32 unique_states = 0;

    timestamp(ts);
    printf("%s: Searching game record files (%s*.sgf)...\n", ts,
        data_folder());
    u32 filenames_found = recurse_find_files(data_folder(), ".sgf",
        filenames, MAX_FILES);
    if(filenames_found == 0)
        printf("No SGF files found.\n");
    else
        printf("Found %u SGF files.\n", filenames_found);

    timestamp(ts);
    printf("%s: Loading game states\n", ts);

    char * buf = malloc(MAX_FILE_SIZ);
    game_record * gr = malloc(sizeof(game_record));

    for(u32 fid = 0; fid < filenames_found; ++fid)
    {
        if(!no_print)
            printf("%u/%u: %s", fid + 1, filenames_found, filenames[fid]);

        if(!import_game_from_sgf2(gr, filenames[fid], buf))
        {
            if(!no_print)
                printf(" skipped\n");
            continue;
        }

        /* Ignore handicap matches */
        if(gr->handicap_stones.count > 0)
        {
            if(!no_print)
                printf(" skipped\n");
            continue;
        }

        board b;
        clear_board(&b);
        bool is_black = true;

        ++games_used;
        if(!no_print)
            printf(" (%u)\n", gr->turns);

        d16 k;
        for(k = 0; k < MIN(ob_depth, gr->turns); ++k)
        {
            move m = gr->moves[k];

            /* Stop at the first play that is either a capture or pass */
            if(!is_board_move(m))
                break;

            u16 caps;
            u8 libs = libs_after_play_slow(&b, is_black, m, &caps);
            if(libs < 1 || caps > 0)
                break;

            board b2;
            memcpy(&b2, &b, sizeof(board));

            if(!attempt_play_slow(&b, is_black, m))
            {
                fprintf(stderr, "\rerror: file contains illegal plays\n");
                exit(EXIT_FAILURE);
            }

            reduce_auto(&b2, true);

            simple_state_transition stmp;
            memset(&stmp, 0, sizeof(simple_state_transition));
            pack_matrix(stmp.p, b2.p);
            stmp.hash = crc32(stmp.p, PACKED_BOARD_SIZ);
            simple_state_transition * entry =
                (simple_state_transition *)hash_table_find(table, &stmp);

            if(entry == NULL) /* new state */
            {
                entry = (simple_state_transition *)malloc(
                    sizeof(simple_state_transition));
                if(entry == NULL){
                    fprintf(stderr, "\rerror: new sst: system out of memory\n");
                    exit(EXIT_FAILURE);
                }
                memset(entry, 0, sizeof(simple_state_transition));
                memcpy(entry->p, stmp.p, PACKED_BOARD_SIZ);
                entry->hash = stmp.hash;
                entry->popularity = 1;

                hash_table_insert(table, entry);
                ++unique_states;
            }
            else
                entry->popularity++;

            is_black = !is_black;
        }
    }


    printf("\nFound %u unique game states from %u games.\n", unique_states,
        games_used);
    if(unique_states == 0)
    {
        release(ts);
        return EXIT_SUCCESS;
    }

    printf("\nSorting by number of occurrences\n");


    simple_state_transition ** ssts =
        (simple_state_transition **)hash_table_export_to_array(table);

    qsort(ssts, unique_states, sizeof(simple_state_transition *),
        sort_cmp_function);

    printf("\nEvaluating game states and saving best play\n");

    char * log_filename = alloc();
    int fd = create_and_open_file(log_filename, MAX_PAGE_SIZ, true, "matilda",
        "ob");
    if(fd == -1)
    {
        timestamp(ts);
        printf("%s: Failed to create output file %s\n", ts, log_filename);
        exit(EXIT_FAILURE);
    }

    timestamp(ts);
    printf("%s: Created output file %s\n", ts, log_filename);
    release(log_filename);

    board b;
    clear_board(&b);
    out_board out_b;

    u32 evaluated = 0;
    sync();

    for(u32 idx = 0; ssts[idx]; ++idx)
    {
        simple_state_transition * sst = ssts[idx];


        timestamp(ts);
        printf("%s: State %u (%u samples)...\n", ts, idx + 1, sst->popularity);

        evaluated++;
        unpack_matrix(b.p, sst->p);
        b.last_eaten = b.last_played = NONE;
        if(opening_book(&out_b, &b))
        {
            timestamp(ts);
            printf("%s: State already present in opening books.\n", ts);
            continue;
        }

        u64 curr_time = current_time_in_millis();
        u32 given = secs_per_turn * 1000;
        u64 stop_time = curr_time + given;
        u64 early_stop_time = curr_time + given / 3;
        mcts_start_timed(&out_b, &b, true, stop_time, early_stop_time);

        out_b.pass = -1.0;
        move best = select_play_fast(&out_b);

        if(!is_board_move(best))
        {
            timestamp(ts);
            printf("%s: Best play is a pass.\n", ts);
            continue;
        }
        tt_clean_all();

        board_to_ob_rule(str, b.p, best);

        timestamp(ts);
        printf("%s", str);

        ssize_t w = write(fd, str, strlen(str));
        if(w == -1)
        {
            fprintf(stderr, "error: write failed\n");
            exit(EXIT_FAILURE);
        }
        sync();
    }
    close(fd);
    printf("Evaluated %u unique states.\n", evaluated);

    hash_table_destroy(table, true);

    timestamp(ts);
    printf("%s: Job done.\n", ts);
    release(ts);
    release(str);
    return EXIT_SUCCESS;
}
