/*
Simple application for grading 3x3 patterns by frequency of selection in SGF
records. The results are written to data/NxN.weights.new.

The number of appearances is not normalized. If a pattern appears multiple times
it will be selected as winner or loser multiple times. In contrast with
considering only unique patterns per state, this does not privilege patterns
that are more common.

The weights are 16-bit values that are later scaled by a factor of 1/9 so their
maximum total on a 3x3 neighborship fits 16 bits.
*/


#include "matilda.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "board.h"
#include "constants.h"
#include "engine.h"
#include "file_io.h"
#include "flog.h"
#include "hash_table.h"
#include "move.h"
#include "pat3.h"
#include "randg.h"
#include "sgf.h"
#include "state_changes.h"
#include "tactical.h"
#include "timem.h"
#include "types.h"


#define MAX_FILES 500000


typedef struct __pat3t_ {
    u16 value;
    u32 wins;
    u32 appearances;
    struct __pat3_ * next;
} pat3t;


static u32 pat3t_hash_function(void * a)
{
    pat3t * b = (pat3t *)a;
    return b->value;
}

static int pat3t_compare_function(
    const void * a,
    const void * b
){
    pat3t * f1 = (pat3t *)a;
    pat3t * f2 = (pat3t *)b;
    return ((d32)(f2->value)) - ((d32)(f1->value));
}

static char * filenames[MAX_FILES];

static u16 get_pattern(
    cfg_board * cb,
    move m
){
    u8 v[3][3];
    pat3_transpose(v, cb->p, m);
    pat3_reduce_auto(v);
    return pat3_to_string((const u8 (*)[3])v);
}

int main(
    int argc,
    char * argv[]
){
    bool no_print = false;

    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "--no_print") == 0){
            no_print = true;
            continue;
        }

        printf("Usage: %s [options]\n", argv[0]);
        printf("Options:\n");
        printf("--no_print - Do not print SGF filenames.\n");
        exit(EXIT_SUCCESS);
    }

    alloc_init();

    flog_config_modes(LOG_MODE_ERROR | LOG_MODE_WARN);
    flog_config_destinations(LOG_DEST_STDF);

    assert_data_folder_exists();
    board_constants_init();

    char * ts = alloc();

    fprintf(stderr, "Discovering game states\n");

    u32 filenames_found = recurse_find_files(get_data_folder(), ".sgf",
        filenames, MAX_FILES);
    if(filenames_found == 0)
    {
        timestamp(ts);
        fprintf(stderr, "%s: No SGF files found, exiting.\n", ts);
        release(ts);
        return EXIT_SUCCESS;
    }
    fprintf(stderr, "\nfound %u SGF files\n", filenames_found);

    timestamp(ts);
    fprintf(stderr, "%s: 2/3 Extracting state plays\n", ts);

    u32 games_skipped = 0;
    u32 games_used = 0;
    u32 unique_patterns = 0;

    hash_table * feature_table = hash_table_create(1543, sizeof(pat3t),
        pat3t_hash_function, pat3t_compare_function);

    char  * buf = malloc(MAX_FILE_SIZ);
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

        ++games_used;
        if(!no_print)
            printf(" (%u)\n", gr->turns);

        for(d16 k = 0; k < gr->turns; ++k)
        {
            move m = gr->moves[k];

            if(m == PASS)
                pass(&b);
            else
            {
                cfg_board cb;
                cfg_from_board(&cb, &b);

                u16 winner_pattern = get_pattern(&cb, m);

                for(move n = 0; n < TOTAL_BOARD_SIZ; ++n)
                {
                    if(cb.p[n] != EMPTY)
                        continue;

                    if(ko_violation(&cb, n))
                        continue;

                    u8 libs;
                    bool captures;
                    if((libs = safe_to_play2(&cb, true, n, &captures))
                        == 0)
                        continue;

                    u16 pattern = get_pattern(&cb, n);

                    pat3t * found = hash_table_find(feature_table, &pattern);
                    if(found == NULL)
                    {
                        found = malloc(sizeof(pat3t));
                        found->value = pattern;
                        found->wins = 0;
                        found->appearances = 0;
                        hash_table_insert_unique(feature_table, found);
                        ++unique_patterns;
                    }

                    found->wins += (pattern == winner_pattern) ? 1 : 0;
                    found->appearances++;
                }

                cfg_board_free(&cb);

                just_play_slow(&b, true, m);
            }

            invert_color(b.p);
        }
    }

    fprintf(stderr, "Games used: %u Skipped: %u\nUnique patterns: %u\n",
        games_used, games_skipped, unique_patterns);

    timestamp(ts);
    fprintf(stderr, "%s: 3/3 Exporting to file\n", ts);

    pat3t ** table = (pat3t **)hash_table_export_to_array(feature_table);

    snprintf(buf, MAX_PAGE_SIZ, "%s%ux%u.weights.new", get_data_folder(),
        BOARD_SIZ, BOARD_SIZ);
    FILE * fp = fopen(buf, "w");
    if(fp == NULL)
    {
        fprintf(stderr, "error: couldn't open file for writing\n");
        exit(EXIT_FAILURE);
    }


    fprintf(fp, "# games used: %u skipped: %u\n# unique patterns: %u\n\n#Hex We\
ight Count\n", games_used, games_skipped, unique_patterns);

    while(*table != NULL)
    {
        pat3t * f = *table;
        ++table;

        double weight = (((double)(f->wins)) / ((double)(f->appearances))) *
        65535.0;
        snprintf(buf, 256, "%04x %5u %u\n", f->value, (u32)weight,
            f->appearances);
        size_t w = fwrite(buf, strlen(buf), 1, fp);
        if(w != 1)
        {
            fprintf(stderr, "error: write failed\n");
            exit(EXIT_FAILURE);
        }
    }

    fclose(fp);

    return EXIT_SUCCESS;
}
