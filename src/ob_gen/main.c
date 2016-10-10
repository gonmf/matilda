/*
Application for the production of Fuego book from SGF game collections.
*/

#include "matilda.h"

#include <unistd.h>
#include <stdlib.h>
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
#include "opening_book.h"
#include "randg.h"
#include "sgf.h"
#include "state_changes.h"
#include "stringm.h"
#include "timem.h"


#define MAX_FILES 500000
#define TABLE_BUCKETS 4957

static char * filenames[MAX_FILES];

static d32 ob_depth = BOARD_SIZ;
static d32 minimum_turns = (BOARD_SIZ + 1);
static d32 minimum_samples = (BOARD_SIZ / 2);

typedef struct __simple_state_transition_ {
    u8 p[PACKED_BOARD_SIZ];
    u32 count[TOTAL_BOARD_SIZ];
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



static u64 get_total_count(
    simple_state_transition * s
){
    u32 ret = 0;
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        ret += s->count[i];
    return ret;
}

/*
Exports internal OB table to simple OB format in file.
*/
static void export_table_as_ob(
    hash_table * table,
    u32 min_samples
){
    char * str = alloc();

    snprintf(str, MAX_PAGE_SIZ, "%s%ux%u.ob.new", data_folder(),
        BOARD_SIZ, BOARD_SIZ);

    FILE * fp = fopen(str, "w");
    if(fp == NULL)
    {
        fprintf(stderr,
            "error: failed to open opening book file for writing\n");
        release(str);
        exit(EXIT_FAILURE);
    }

    u32 skipped = 0;
    u32 exported = 0;

    simple_state_transition ** ssts =
        (simple_state_transition **)hash_table_export_to_array(table);

    for(u32 idx = 0; ssts[idx]; ++idx)
    {
        simple_state_transition * h = ssts[idx];

        u32 total_count = get_total_count(h);
        if(total_count < min_samples)
        {
            ++skipped;
            continue;
        }

        u32 best_count = 0;
        move best = NONE;
        for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
            if(h->count[i] > best_count)
            {
                best_count = h->count[i];
                best = i;
            }

        if(best == NONE)
        {
            fprintf(stderr, "error: unexpected absence of samples\n");
            release(str);
            exit(EXIT_FAILURE);
        }

        if(best_count <= total_count / 2)
        {
            ++skipped;
            continue;
        }

        u8 p[TOTAL_BOARD_SIZ];
        unpack_matrix(p, h->p);

        board_to_ob_rule(str, p, best);
        size_t w = fwrite(str, strlen(str), 1, fp);
        if(w != 1)
        {
            fprintf(stderr, "error: write failed\n");
            release(str);
            exit(EXIT_FAILURE);
        }

        ++exported;
    }

    size_t w = fwrite(str, strlen(str), 1, fp);
    release(str);
    if(w != 1)
    {
        fprintf(stderr, "error: write failed\n");
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    printf("Exported %u unique rules; %u were disqualified for not enough sampl\
es or majority representative\n", exported, skipped);
}

int main(
    int argc,
    char * argv[]
){
    bool no_print = false;

    for(int i = 1; i < argc; ++i)
    {
        if(i < argc - 1 && strcmp(argv[i], "--max_depth") == 0)
        {
            d32 a;
            if(!parse_int(&a, argv[i + 1]) || a < 1)
                goto lbl_usage;
            ++i;
            ob_depth = a;
            continue;
        }
        if(i < argc - 1 && strcmp(argv[i], "--min_game_turns") == 0)
        {
            d32 a;
            if(!parse_int(&a, argv[i + 1]) || a < 1)
                goto lbl_usage;
            ++i;
            minimum_turns = a;
            continue;
        }
        if(i < argc - 1 && strcmp(argv[i], "--min_samples") == 0)
        {
            d32 a;
            if(!parse_int(&a, argv[i + 1]) || a < 1)
                goto lbl_usage;
            ++i;
            minimum_samples = a;
            continue;
        }
        if(strcmp(argv[i], "--no_print") == 0){
            no_print = true;
            continue;
        }

lbl_usage:
        printf("Usage: %s [options]\n", argv[0]);
        printf("Options:\n");
        printf("--max_depth number - Maximum turn depth of the openings. (defau\
lt: %u)\n", ob_depth);
        printf("--min_game_turns number - Minimum number of turns for the game \
to be used. (default: %u)\n", minimum_turns);
        printf("--min_samples - Minimum number of samples for a rule to be save\
d. (default: %u)\n", minimum_samples);
        printf("--no_print - Do not print SGF filenames.\n");
        exit(EXIT_SUCCESS);
    }

    alloc_init();

    flog_config_modes(LOG_MODE_ERROR | LOG_MODE_WARN);
    flog_config_destinations(LOG_DEST_STDF);

    assert_data_folder_exists();

    char * ts = alloc();
    timestamp(ts);


    printf("%s: Creating table...\n", ts);
    hash_table * table = hash_table_create(TABLE_BUCKETS,
        sizeof(simple_state_transition), hash_function, compare_function);



    u32 games_used = 0;
    u32 plays_used = 0;
    u32 ob_rules = 0;

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
    printf("%s: 1/2 Thinking\n", ts);

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

        if(gr->turns < minimum_turns){
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

        ++games_used;
        if(!no_print)
            printf(" (%u)\n", gr->turns);

        bool is_black = true;
        for(d16 k = 0; k < MIN(ob_depth, gr->turns); ++k)
        {
            move m = gr->moves[k];

            /* Stop at the first play that is a pass */
            if(!is_board_move(m))
                break;

            u16 caps;
            u8 libs = libs_after_play_slow(&b, is_black, m, &caps);
            if(libs < 1 || caps > 0)
                break;

            ++plays_used;

            board b2;
            memcpy(&b2, &b, sizeof(board));

            if(!attempt_play_slow(&b, is_black, m))
            {
                fprintf(stderr, "\rerror: file contains illegal plays\n");
                exit(EXIT_FAILURE);
            }

            d8 reduction = reduce_auto(&b2, is_black);
            m = reduce_move(m, reduction);

            simple_state_transition stmp;
            memset(&stmp, 0, sizeof(simple_state_transition));
            pack_matrix(stmp.p, b2.p);
            stmp.hash = crc32(stmp.p, PACKED_BOARD_SIZ);

            simple_state_transition * entry =
                (simple_state_transition *)hash_table_find(table, &stmp);

            if(entry == NULL) /* new state */
            {
                simple_state_transition * entry = (simple_state_transition
                    *)malloc(sizeof(simple_state_transition));
                if(entry == NULL)
                {
                    fprintf(stderr, "\rerror: new sst: system out of memory\n");
                    exit(EXIT_FAILURE);
                }
                memset(entry, 0, sizeof(simple_state_transition));
                memcpy(entry->p, stmp.p, PACKED_BOARD_SIZ);
                entry->hash = stmp.hash;
                entry->count[m] = 1;

                hash_table_insert(table, entry);
                ++ob_rules;
            }
            else /* reusing state */
                entry->count[m]++;

            is_black = !is_black;
        }
    }

    printf("\n\n");

    if(ob_rules == 0)
    {
        printf("No rules found; nothing to do. Closing.\n");
        return EXIT_SUCCESS;
    }

    timestamp(ts);
    printf("%s: 2/2 Exporting as opening book...\n", ts);

    export_table_as_ob(table, minimum_samples);

    hash_table_destroy(table, true);

    timestamp(ts);
    printf("%s: Job done.\n", ts);

    release(ts);
    return EXIT_SUCCESS;
}
