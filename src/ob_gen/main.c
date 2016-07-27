/*
Application for the production of Fuego book from SGF game collections.
*/

#include "matilda.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "engine.h"
#include "file_io.h"
#include "crc32.h"
#include "timem.h"
#include "opening_book.h"
#include "randg.h"
#include "sgf.h"
#include "simple_state.h"
#include "state_changes.h"
#include "stringm.h"
#include "buffer.h"

#define MAX_FILES 500000

static char * filenames[MAX_FILES];

static bool relax_komi = true;
static s32 ob_depth = BOARD_SIZ;
static s32 minimum_turns = (BOARD_SIZ + 1);
static s32 minimum_samples = (BOARD_SIZ / 2);



int main(
    int argc,
    char * argv[]
){
    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-version") == 0)
        {
            printf("matilda %u.%u\n", VERSION_MAJOR, VERSION_MINOR);
            exit(EXIT_SUCCESS);
        }
        if(i < argc - 1 && strcmp(argv[i], "-max_depth") == 0)
        {
            s32 a;
            if(!parse_int(argv[i + 1], &a) || a < 1)
                goto usage;
            ++i;
            ob_depth = a;
            continue;
        }
        if(i < argc - 1 && strcmp(argv[i], "-min_game_turns") == 0)
        {
            s32 a;
            if(!parse_int(argv[i + 1], &a) || a < 1)
                goto usage;
            ++i;
            minimum_turns = a;
            continue;
        }
        if(i < argc - 1 && strcmp(argv[i], "-min_samples") == 0)
        {
            s32 a;
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

    timestamp();
    assert_data_folder_exists();
    simple_state_table_init();


    u32 games_used = 0;
    u32 games_skipped = 0;
    u32 plays_used = 0;
    u32 passes = 0;
    u32 ob_rules = 0;

    printf("%s: Searching game record files (%s*.sgf)...\n", timestamp(),
        get_data_folder());
    u32 filenames_found = recurse_find_files(get_data_folder(), ".sgf",
        filenames, MAX_FILES);
    if(filenames_found == 0)
        printf("%s: No SGF files found.\n", timestamp());
    else
        printf("%s: Found %u SGF files.\n", timestamp(), filenames_found);

    printf("%s: 1/2 Thinking\n", timestamp());
    u32 fid;
    for(fid = 0; fid < filenames_found; ++fid)
    {
        if((fid % 2048) == 0)
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

        s16 plays_count = sgf_to_boards(buf, plays, &irregular_play_order);
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

        s16 k;
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
                if(entry == NULL)
                {
                    fprintf(stderr, "\rerror: new sst: system out of memory\n");
                    exit(EXIT_FAILURE);
                }
                memcpy(entry->p, packed_board, PACKED_BOARD_SIZ);
                memset(entry->count, 0, sizeof(u32) * BOARD_SIZ * BOARD_SIZ);
                entry->hash = hash;
                entry->count[plays[k]] = 1;
                simple_state_collection_add(entry);
                ++ob_rules;
            }
            else /* reusing state */
                entry->count[plays[k]]++;
        }
    }

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
    printf("%s: 2/2 Exporting as opening book...\n", timestamp());

    simple_state_collection_export(minimum_samples);

    printf("%s: Job done.\n", timestamp());
    return EXIT_SUCCESS;
}
