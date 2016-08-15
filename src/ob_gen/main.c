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
#include "hash_table.h"
#include "opening_book.h"
#include "randg.h"
#include "sgf.h"
#include "state_changes.h"
#include "stringm.h"
#include "timem.h"
#include "version.h"


#define MAX_FILES 500000
#define TABLE_BUCKETS 4957

static char * filenames[MAX_FILES];

static bool relax_komi = true;
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

static d32 compare_function(
    void * o1,
    void * o2
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

    snprintf(str, MAX_PAGE_SIZ, "%soutput.ob", get_data_folder());

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
    u32 idx;

    for(u32 ti = 0; ti < TABLE_BUCKETS; ++ti)
    {
        ht_node * node = table->table[ti];
        while(node != NULL && node->data != NULL)
        {
            simple_state_transition * h = (simple_state_transition *)node->data;

            u32 total_count = get_total_count(h);
            if(total_count < min_samples)
            {
                ++skipped;
                node = node->next;
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
                node = node->next;
                continue;
            }

            u8 p[TOTAL_BOARD_SIZ];
            unpack_matrix(p, h->p);

            move m1 = 0;
            move m2 = 0;
            bool is_black = true;


            idx = snprintf(str, MAX_PAGE_SIZ, "%u ", BOARD_SIZ);

            char * mstr = alloc();
            while(1)
            {
                bool found = false;
                if(is_black)
                {
                    for(; m1 < TOTAL_BOARD_SIZ; ++m1)
                        if(p[m1] == BLACK_STONE)
                        {
                            coord_to_alpha_num(mstr, m1);
                            idx += snprintf(str + idx, MAX_PAGE_SIZ - idx,
                                "%s ", mstr);
                            found = true;
                            ++m1;
                            break;
                        }
                }else
                    for(; m2 < TOTAL_BOARD_SIZ; ++m2)
                        if(p[m2] == WHITE_STONE)
                        {
                            coord_to_alpha_num(mstr, m2);
                            idx += snprintf(str + idx, MAX_PAGE_SIZ - idx,
                                "%s ", mstr);
                            found = true;
                            ++m2;
                            break;
                        }
                if(!found)
                    break;
            }

            coord_to_alpha_num(mstr, best);
            snprintf(str + idx, MAX_PAGE_SIZ - idx, "| %s # %u/%u\n", mstr,
                best_count, total_count);
            release(mstr);

            size_t w = fwrite(str, strlen(str), 1, fp);
            if(w != 1)
            {
                fprintf(stderr, "error: write failed\n");
                release(str);
                exit(EXIT_FAILURE);
            }

            ++exported;
            node = node->next;
        }
    }

    snprintf(str, MAX_PAGE_SIZ, "# exported %u unique rules; %u were disqualifi\
ed for not enough samples or majority representative\n",exported, skipped);
    size_t w = fwrite(str, strlen(str), 1, fp);
    release(str);
    if(w != 1)
    {
        fprintf(stderr, "error: write failed\n");
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    printf("exported %u unique rules; %u were disqualified for not enough sampl\
es or majority representative\n", exported, skipped);
}

int main(
    int argc,
    char * argv[]
){
    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-version") == 0)
        {
            printf("matilda %s\n", MATILDA_VERSION);
            exit(EXIT_SUCCESS);
        }
        if(i < argc - 1 && strcmp(argv[i], "-max_depth") == 0)
        {
            d32 a;
            if(!parse_int(argv[i + 1], &a) || a < 1)
                goto usage;
            ++i;
            ob_depth = a;
            continue;
        }
        if(i < argc - 1 && strcmp(argv[i], "-min_game_turns") == 0)
        {
            d32 a;
            if(!parse_int(argv[i + 1], &a) || a < 1)
                goto usage;
            ++i;
            minimum_turns = a;
            continue;
        }
        if(i < argc - 1 && strcmp(argv[i], "-min_samples") == 0)
        {
            d32 a;
            if(!parse_int(argv[i + 1], &a) || a < 1)
                goto usage;
            ++i;
            minimum_samples = a;
            continue;
        }
        if(strcmp(argv[i], "-relax_komi") == 0)
        {
            relax_komi = true;
            continue;
        }

usage:
        printf("Usage: %s [options]\n", argv[0]);
        printf("Options:\n");
        printf("-max_depth number - Maximum turn depth of the openings. (defaul\
t: %u)\n", ob_depth);
        printf("-min_game_turns number - Minimum number of turns for the game t\
o be used. (default: %u)\n", minimum_turns);
        printf("-min_samples - Minimum number of samples for a rule to be saved\
. (default: %u)\n", minimum_samples);
        printf("-relax_komi - Allow games with uncommon komi values.\n");
        printf("-version - Print version information and exit.\n");
        exit(EXIT_SUCCESS);
    }

    alloc_init();
    assert_data_folder_exists();

    char * ts = alloc();
    timestamp(ts);


    printf("%s: Creating table...\n", ts);
    hash_table * table = hash_table_create(TABLE_BUCKETS,
        sizeof(simple_state_transition), hash_function, compare_function);



    u32 games_used = 0;
    u32 games_skipped = 0;
    u32 plays_used = 0;
    u32 passes = 0;
    u32 ob_rules = 0;

    timestamp(ts);
    printf("%s: Searching game record files (%s*.sgf)...\n", ts,
        get_data_folder());
    u32 filenames_found = recurse_find_files(get_data_folder(), ".sgf",
        filenames, MAX_FILES);

    if(filenames_found == 0)
        printf("No SGF files found.\n");
    else
        printf("Found %u SGF files.\n", filenames_found);

    timestamp(ts);
    printf("%s: 1/2 Thinking\n", ts);

    char * buf = alloc();
    u32 fid;
    for(fid = 0; fid < filenames_found; ++fid)
    {
        if((fid % 2048) == 0)
        {
            printf("\r%u%%", ((fid + 1) * 100) / filenames_found);
            fflush(stdout);
        }

        d32 r = read_ascii_file(buf, MAX_PAGE_SIZ, filenames[fid]);
        if(r <= 0 || r >= MAX_PAGE_SIZ)
        {
            fprintf(stderr, "\rerror: unexpected file size or read error\n");
            exit(EXIT_FAILURE);
        }
        buf[r] = 0;

        move plays[MAX_GAME_LENGTH];

        bool black_won;
        bool _ignored;
        bool normal_komi;

        if(!sgf_info(buf, &black_won, &_ignored, &_ignored, &normal_komi))
        {
            ++games_skipped;
            continue;
        }
        if(!relax_komi && !normal_komi)
        {
            ++games_skipped;
            continue;
        }
        bool irregular_play_order;

        d16 plays_count = sgf_to_boards(buf, plays, &irregular_play_order);
        if(plays_count == -1 || plays_count < minimum_turns)
        {
            ++games_skipped;
            continue;
        }
        if(irregular_play_order)
        {
            ++games_skipped;
            continue;
        }
        ++games_used;

        board b;
        clear_board(&b);

        d16 k;
        for(k = 0; k < MIN(ob_depth, plays_count); ++k)
        {
            if(plays[k] == PASS)
                break;

            ++plays_used;

            if(b.p[plays[k]] != EMPTY)
            {
                fprintf(stderr, "\rerror: file contains plays over stones\n");
                exit(EXIT_FAILURE);
            }

            bool is_black = (k & 1) == 0;

            board b2;
            memcpy(&b2, &b, sizeof(board));

            if(!attempt_play_slow(&b, plays[k], is_black))
            {
                fprintf(stderr, "\rerror: file contains illegal plays\n");
                exit(EXIT_FAILURE);
            }

            if(b.last_eaten != NONE)
                break;

            d8 reduction = reduce_auto(&b2, is_black);
            plays[k] = reduce_move(plays[k], reduction);

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
                entry->count[plays[k]] = 1;

                hash_table_insert(table, entry);
                ++ob_rules;
            }
            else /* reusing state */
                entry->count[plays[k]]++;
        }
    }
    release(buf);

    printf("\n\n");

    if(ob_rules == 0)
    {
        printf("No rules found; nothing to do. Closing.\n");
        return EXIT_SUCCESS;
    }

    printf("matches found=%u used=%u skipped=%u\nconsidered plays=%u and passes\
=%u\nunique ob rules=%u (from first %u turns)\n", games_used + games_skipped,
        games_used, games_skipped, plays_used, passes, ob_rules, ob_depth);

    printf("\n");
    timestamp(ts);
    printf("%s: 2/2 Exporting as opening book...\n", ts);

    export_table_as_ob(table, minimum_samples);

    hash_table_destroy(table, true);

    timestamp(ts);
    printf("%s: Job done.\n", ts);

    release(ts);
    return EXIT_SUCCESS;
}
