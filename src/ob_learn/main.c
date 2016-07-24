/*
Program for the evaluation of positions from game records and the production of
a state->play file (.spb), to be used for further play suggestions besides
(Fuego-style) opening books.
*/


#include "matilda.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "crc32.h"
#include "engine.h"
#include "file_io.h"
#include "mcts.h"
#include "opening_book.h"
#include "randg.h"
#include "sgf.h"
#include "simple_state.h"
#include "state_changes.h"
#include "stringm.h"
#include "time.h"
#include "timem.h"
#include "transpositions.h"
#include "zobrist.h"
#include "buffer.h"

#define SECS_PER_TURN 30

#define MAX_FILES 500000

static char * filenames[MAX_FILES];

static bool relax_komi = true;
static s32 ob_depth = (BOARD_SIZ * BOARD_SIZ) / 2;
static s32 minimum_samples = 32;


int main(int argc, char * argv[]){
    for(int i = 1; i < argc; ++i){
        if(strcmp(argv[i], "-version") == 0){
            printf("matilda %u.%u\n", VERSION_MAJOR, VERSION_MINOR);
            exit(EXIT_SUCCESS);
        }
        if(i < argc - 1 && strcmp(argv[i], "-max_depth") == 0){
            s32 a;
            if(!parse_int(argv[i + 1], &a) || a < 1)
                goto usage;
            ++i;
            ob_depth = a;
            continue;
        }
        if(i < argc - 1 && strcmp(argv[i], "-min_samples") == 0){
            s32 a;
            if(!parse_int(argv[i + 1], &a) || a < 1)
                goto usage;
            ++i;
            minimum_samples = a;
            continue;
        }
        if(strcmp(argv[i], "-relax_komi") == 0){
            relax_komi = true;
            continue;
        }

usage:
        printf("Usage: %s [options]\n", argv[0]);
        printf("Options:\n");
        printf("-max_depth number - Maximum turn depth of the openings. (defaul\
t: %u)\n", ob_depth);
        printf("-min_samples - Minimum number of samples for a rule to be saved\
. (default: %u)\n", minimum_samples);
        printf("-relax_komi - Allow games with uncommon komi values.\n");
        printf("-version - Print version information and exit.\n");
        exit(EXIT_SUCCESS);
    }

    timestamp();
    rand_init();
    assert_data_folder_exists();
    simple_state_table_init();
    cfg_board_init();
    zobrist_init();
    transpositions_table_init();


    u32 games_used = 0;
    u32 unique_states = 0;

    printf("%s: Searching game record files (%s*.sgf)...\n", timestamp(),
        get_data_folder());
    u32 filenames_found = recurse_find_files(get_data_folder(), ".sgf",
        filenames, MAX_FILES);
    if(filenames_found == 0)
        printf("%s: No SGF files found.\n", timestamp());
    else
        printf("%s: Found %u SGF files.\n", timestamp(), filenames_found);

    printf("%s: 1/2 Loading game states\n", timestamp());
    u32 fid;
    for(fid = 0; fid < filenames_found; ++fid)
    {
        if((fid % 512) == 0)
        {
            printf("\r%u%%", ((fid + 1) * 100) / filenames_found);
            fflush(stdout);
        }

        char * buf = get_buffer();
        s32 r = read_ascii_file(filenames[fid], buf, MAX_PAGE_SIZ);
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

        if(!sgf_info(buf, &black_won, &_ignored, &_ignored, &normal_komi)){
            continue;
        }
        if(!relax_komi && !normal_komi){
            continue;
        }
        bool irregular_play_order;

        s16 plays_count = sgf_to_boards(buf, plays, &irregular_play_order);
        ++games_used;

        board b;
        clear_board(&b);

        s16 k;
        for(k = 0; k < MIN(ob_depth, plays_count); ++k)
        {
            if(plays[k] == PASS)
            {
                pass(&b);
                continue;
            }

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
                continue;

            s8 reduction = reduce_auto(&b2, is_black);
            plays[k] = reduce_move(plays[k], reduction);

            u8 packed_board[PACKED_BOARD_SIZ];
            pack_matrix(b2.p, packed_board);
            u32 hash = crc32(packed_board, PACKED_BOARD_SIZ);
            simple_state_transition * entry = simple_state_collection_find(hash,
                packed_board);
            if(entry == NULL) /* new state */
            {
                simple_state_transition * entry = (simple_state_transition
                    *)malloc(sizeof(simple_state_transition));
                if(entry == NULL){
                    fprintf(stderr, "\rerror: new sst: system out of memory\n");
                    exit(EXIT_FAILURE);
                }
                memcpy(entry->p, packed_board, PACKED_BOARD_SIZ);
                entry->hash = hash;
                entry->play = plays[k];
                entry->popularity = 1;
                simple_state_collection_add(entry);
                ++unique_states;
            }
            else
                entry->popularity++;
        }
    }

    printf("\nFound %u unique game states from %u games.\n", unique_states,
        games_used);
    if(unique_states == 0)
        return EXIT_SUCCESS;

    printf("\n%s: 2/2 Evaluating game states and saving best play\n",
        timestamp());

    simple_state_transition * sst = simple_state_collection_export();

    char * filename = get_buffer();
    snprintf(filename, 128, "%soutput.spb", get_data_folder());
    printf("%s: Created output file %s\n\n\n", timestamp(), filename);
    FILE * fp = fopen(filename, "w");


    board b;
    clear_board(&b);
    out_board out_b;
    opening_book(&b, &out_b);

    u32 evaluated = 0;

    while(sst != NULL)
    {
        if(sst->popularity >= (u32)minimum_samples)
        {
            evaluated++;

            unpack_matrix(b.p, sst->p);
            b.last_eaten = b.last_played = NONE;
            fprint_board(stdout, &b);

            if(opening_book(&b, &out_b))
            {
                printf("%s: State already present in opening books.\n",
                    timestamp());
                sst = sst->next;
                continue;
            }

            u64 curr_time = current_time_in_millis();
            u64 stop_time = curr_time + SECS_PER_TURN * 1000;
            mcts_start(&b, true, &out_b, stop_time, stop_time);

            move best = select_play_fast(&out_b);
            tt_clean_all();

            if(!is_board_move(best))
            {
                printf("%s: Best play was to pass; ignored.\n", timestamp());
                sst = sst->next;
                continue;
            }

            char * str = get_buffer();
            u32 idx = 0;
            idx += snprintf(str + idx, 1024 - idx, "%u ", BOARD_SIZ);

            for(move m = 0; m < BOARD_SIZ * BOARD_SIZ; ++m)
            {
                if(b.p[m] == BLACK_STONE)
                    idx += snprintf(str + idx, 1024 - idx, "X");
                else
                    if(b.p[m] == WHITE_STONE)
                        idx += snprintf(str + idx, 1024 - idx, "O");
                    else
                        idx += snprintf(str + idx, 1024 - idx, ".");
            }

            snprintf(str + idx, 1024 - idx, " %s\n", coord_to_alpha_num(best));


            size_t w = fwrite(str, strlen(str), 1, fp);
            if(w != 1)
            {
                fprintf(stderr, "error: write failed\n");
                exit(EXIT_FAILURE);
            }
            fflush(fp);
            fprintf(stderr, "%s", str);

            printf("%s: Best play: %s", timestamp(), coord_to_alpha_num(best));
            printf(" Actual play: %s\n\n\n", coord_to_alpha_num(sst->play));
        }
        sst = sst->next;
    }
    fclose(fp);
    printf("Evaluated %u states unique states.\n", evaluated);

    printf("%s: Job done.\n", timestamp());
    return EXIT_SUCCESS;
}
