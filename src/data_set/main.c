/*
Application for extracting a training set of unique Go play cases


This application consumes SGF files from the data folder and produces a training
set file (.ds) in the same folder. The cases are unique; always from the black
players point of view. Every example is reduced: rotated and flipped to catch
similar states and thus reduce repeated information in the resulting data set
file, which will already be large in size. Every example already codifies
illegal intersections and number of liberties after playing at each empty
intersection.

A training set file consists of 4 bytes with the number of entries in the
training set, followed by the entries in binary form.

Please avoid feeding SGF files of the wrong board size, ruleset, etc since the
application might not be able to catch all possible exceptions. Only matches
with Chinese rules and no handicap stones currently are used. Passes are also
ignored.

*/

#include "matilda.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "board.h"
#include "complete_state.h"
#include "crc32.h"
#include "data_set.h"
#include "engine.h"
#include "file_io.h"
#include "hash_table.h"
#include "neural_network.h" /* for stone codification */
#include "randg.h"
#include "sgf.h"
#include "state_changes.h"
#include "stringm.h"
#include "timem.h"


#define MAX_FILES 500000

static char * filenames[MAX_FILES];

int main(){
    assert_data_folder_exists();
    cs_table_init();

    printf("DATASET COMPILER\n\n");

    u32 games_used = 0;
    u32 games_skipped = 0;
    u32 plays_used = 0;
    u32 passes = 0;

    printf("1/3 Discovering SGF files\n");
    u32 filenames_found = recurse_find_files(data_folder(), ".sgf", filenames,
        MAX_FILES);
    if(filenames_found == 0)
    {
        printf("No SGF files found, exiting.\n");
        return EXIT_SUCCESS;
    }
    printf("\nfound %u SGF files\n", filenames_found);

    printf("2/3 Extracting state plays\n");

    u32 min_plays = MAX_FILE_SIZ;
    u32 max_plays = 0;

    u32 uniques = 0;
    char * buf = alloc();
    char * file_buf = malloc(MAX_FILE_SIZ);
    if(file_buf == NULL)
    {
        fprintf(stderr, "error: out of memory exception\n");
        return EXIT_FAILURE;
    }
    game_record gr;

    u32 fid;
    for(fid = 0; fid < filenames_found; ++fid)
    {
        // TODO do not use handicap games

        // TODO use a division of the filenames found instead of 2048
        if((fid % 2048) == 0)
        {
            printf("\r %u%%", ((fid + 1) * 100) / filenames_found);
            fflush(stdout);
        }

        if(!import_game_from_sgf2(&gr, filenames[fid], file_buf))
        {
            ++games_skipped;
            continue;
        }

        ++games_used;

        if(gr.turns < min_plays)
            min_plays = gr.turns;
        if(gr.turns > max_plays)
            max_plays = gr.turns;

        board b;
        clear_board(&b);
        bool is_black = false;

        /* ignoring possible outliers */
        for(d16 k = 0; k < gr.turns && stone_count(b.p) <= TOTAL_BOARD_SIZ / 3;
            ++k)
        {
            is_black = !is_black;
            move m = gr.moves[k];

            if(m == PASS)
            {
                pass(&b);
                ++passes;
                continue;
            }

            if(b.p[m] != EMPTY)
            {
                fprintf(stderr, "filename: %s\n", filenames[fid]);
                fprint_board(stderr, &b);
                fprintf(stderr, "error: file contains plays over stones\n");
                exit(EXIT_FAILURE);
            }

            board codified_board;
            codified_board.last_eaten = codified_board.last_played = NONE;
            nn_codify_board(codified_board.p, &b, is_black);

            if(codified_board.p[m] == NN_ILLEGAL ||
                codified_board.p[m] == NN_BLACK_STONE)
            {
                fprintf(stderr, "error: file contains impossible plays\n");
                exit(EXIT_FAILURE);
            }

            if(!attempt_play_slow(&b, is_black, m))
            {
                fprintf(stderr, "error: file contains illegal plays\n");
                exit(EXIT_FAILURE);
            }

            ++plays_used;
            d8 reduction = reduce_auto(&codified_board, true);
            m = reduce_move(m, reduction);

            complete_state_transition * new_cs =
                (complete_state_transition *)malloc(sizeof(
                complete_state_transition));
            if(new_cs == NULL){
                fprintf(stderr, "error: new cst: system out of memory\n");
                exit(EXIT_FAILURE);
            }
            memcpy(&new_cs->p, &codified_board.p, TOTAL_BOARD_SIZ);

            complete_state_transition * f = complete_state_collection_find(
                new_cs);
            if(f != NULL){
                free(new_cs);
                f->count[m]++;
            }else{
                memset(&new_cs->count, 0, TOTAL_BOARD_SIZ * sizeof(u32));
                new_cs->count[m] = 1;
                complete_state_collection_add(new_cs);
                uniques++;
            }
        }
    }
    release(buf);

    printf("\n\n");

    printf("\tmatches found=%u used=%u skipped=%u\n", games_used +
        games_skipped, games_used, games_skipped);
    printf("\tplays from 1st 1/3rd=%u (%u unique, %u passes)\n", plays_used,
        uniques, passes);
    printf("\tmaximum number of plays in a match=%d minimum=%d\n", max_plays,
        min_plays);

    if(uniques == 0)
    {
        printf("No rules found, exiting.\n");
        return EXIT_SUCCESS;
    }

    printf("3/3 Writing training set\n");

    char * filename = alloc();
    snprintf(filename, MAX_PAGE_SIZ, "%s%dx%d.ds.txt", data_folder(), BOARD_SIZ,
        BOARD_SIZ);
    FILE * logf = fopen(filename, "w"); /* text file */
    release(filename);
    if(logf == NULL)
    {
        fprintf(stderr, "error: couldn't open file for writing\n");
        exit(EXIT_FAILURE);
    }
    fprintf(logf, "Training set composed of %u unique cases.\nBuilt from %u ful\
l matches, of which %u met the requirements.\n", uniques, filenames_found,
        games_used);

    fprintf(logf, "\tmatches found=%u used=%u skipped=%u\n", games_used +
        games_skipped, games_used, games_skipped);
    fprintf(logf, "\tplays from 1st 1/3rd=%u (%u unique, %u passes)\n",
        plays_used, uniques, passes);
    fprintf(logf, "\tmaximum number of plays in a match=%d minimum=%d\n",
        max_plays, min_plays);
    fclose(logf);

    complete_state_collection_export_as_data_set(uniques);

    printf("Job done.\n");
    return 0;
}
