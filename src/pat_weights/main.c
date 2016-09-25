/*
Simple application for grading 3x3 patterns by frequency of selection in SGF
records. The results are printed to data/pat3_weights.
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

int main()
{
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

    u32 fid;
    for(fid = 0; fid < filenames_found; ++fid)
    {
        if((fid % 128) == 0)
        {
            fprintf(stderr, "\r %u%%", ((fid + 1) * 100) / filenames_found);
            fflush(stdout);
        }

        d32 r = read_ascii_file(buf, MAX_PAGE_SIZ, filenames[fid]);
        if(r <= 0 || r >= MAX_PAGE_SIZ)
        {
            fprintf(stderr, "error: unexpected file size\n");
            exit(EXIT_FAILURE);
        }
        buf[r] = 0;

        move plays[MAX_GAME_LENGTH];
        bool black_won;

        if(!sgf_info(buf, &black_won))
        {
            ++games_skipped;
            continue;
        }

        bool irregular_play_order;

        d16 plays_count = sgf_to_boards(buf, plays, &irregular_play_order);
        if(plays_count == -1)
        {
            ++games_skipped;
            continue;
        }
        if(irregular_play_order)
            fprintf(stderr, "warning: SGF file contains play out of order\n");

        ++games_used;

        board b;
        clear_board(&b);

        for(u16 k = 0; k < plays_count; ++k)
        {
            if(plays[k] == PASS)
                pass(&b);
            else
            {
                if(is_board_move(b.last_played))
                {
                    cfg_board cb;
                    cfg_from_board(&cb, &b);

                    bool valid[TOTAL_BOARD_SIZ];
                    memset(valid, true, TOTAL_BOARD_SIZ);

                    u8 x;
                    u8 y;
                    move_to_coord(b.last_played, &x, &y);

                    for(d8 i = x - 1; i <= x + 1; ++i)
                        for(d8 j = y - 1; j <= y + 1; ++j)
                        {
                            if(i < 0 || i >= BOARD_SIZ || j < 0 || j >=
                                BOARD_SIZ)
                                continue;

                            move m = coord_to_move(i, j);
                            if(b.last_played == m)
                                continue;


                            if(cb.p[m] != EMPTY)
                                continue;

                            if(ko_violation(&cb, m))
                                continue;

                            u8 libs;
                            bool captures;
                            if((libs = safe_to_play2(&cb, true, m, &captures))
                                == 0)
                                continue;

                            u8 v[3][3];
                            pat3_transpose(v, cb.p, m);

                            pat3_reduce_auto(v);
                            u16 pattern = pat3_to_string((const u8 (*)[3])v);

                            pat3t * found = hash_table_find(feature_table,
                                &pattern);
                            if(found == NULL)
                            {
                                pat3t * f = malloc(sizeof(pat3t));
                                f->value = pattern;
                                f->wins = (m == plays[k]) ? 1 : 0;
                                f->appearances = 1;
                                hash_table_insert_unique(feature_table, f);
                                ++unique_patterns;
                            }
                            else
                            {
                                found->wins += (m == plays[k]) ? 1 : 0;;
                                found->appearances++;
                            }
                        }

                    cfg_board_free(&cb);
                }

                just_play_slow(&b, true, plays[k]);
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
