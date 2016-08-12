#include "matilda.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <omp.h>

#include "board.h"
#include "cfg_board.h"
#include "engine.h"
#include "game_record.h"
#include "zobrist.h"
#include "pat3.h"
#include "primes.h"
#include "randg.h"
#include "random_play.h"
#include "state_changes.h"
#include "tactical.h"
#include "timem.h"
#include "types.h"
#include "flog.h"
#include "alloc.h"

extern u16 iv_3x3[TOTAL_BOARD_SIZ][TOTAL_BOARD_SIZ][3];

static char _ts[MAX_PAGE_SIZ];
static char * _timestamp(){
    timestamp(_ts);
    return _ts;
}

static void massert(
    bool expr,
    const char * message
){
    if(!expr)
    {
        if(message != NULL)
            fprintf(stderr, "\nError: %s\n\n", message);
        exit(EXIT_FAILURE);
    }
}

static void test_cfg_board()
{
    fprintf(stderr, "%s: cfg_board operations...", _timestamp());
    u32 tests = BOARD_SIZ > 16 ? 50 : (BOARD_SIZ > 12 ? 200 : 1000);
    for(u32 tes = 0; tes < tests; ++tes)
    {
        board b;
        clear_board(&b);
        cfg_board cb;
        cfg_from_board(&cb, &b);
        massert(cfg_board_are_equal(&cb, &b), "cfg_from_board");

        bool is_black = true;
        for(u16 i = 0; i <= TOTAL_BOARD_SIZ; ++i)
        {
            u16 m = rand_u16(TOTAL_BOARD_SIZ);

            cfg_board sb2;
            cfg_board sb3;
            cfg_from_board(&sb2, &b);
            cfg_board_clone(&sb3, &sb2);

            bool can_play1 = attempt_play_slow(&b, m, is_black);
            bool can_play2 = can_play(&cb, m, is_black);
            if(can_play1 != can_play2)
            {
                fprintf(stderr, "play legality disagreement at %u (s=%u cfg=%u)\
\n", m, can_play1, can_play2);
                fprint_board(stdout, &b);
                fprint_cfg_board(stdout, &cb);
                exit(EXIT_FAILURE);
            }
            if(!can_play1)
            {
                cfg_board_free(&sb2);
                cfg_board_free(&sb3);
                continue;
            }

            d16 stone_diff = 0;
            just_play1(&cb, m, is_black, &stone_diff);
            massert(cfg_board_are_equal(&cb, &b), "just_play");

            just_play(&sb2, m, is_black);
            massert(cfg_board_are_equal(&sb2, &b), "just_play2");
            bool stones_cap[TOTAL_BOARD_SIZ];
            memset(stones_cap, 0, TOTAL_BOARD_SIZ);
            u8 tmp4[LIB_BITMAP_SIZ];
            d16 stone_diff2 = 0;
            just_play3(&sb3, m, is_black, &stone_diff2, stones_cap, tmp4);
            massert(cfg_board_are_equal(&sb3, &b), "just_play3");

            cfg_board_free(&sb2);
            cfg_board_free(&sb3);

            u16 stones_captured4 = 0;
            for(move k = 0; k < TOTAL_BOARD_SIZ; ++k)
                if(stones_cap[k])
                    ++stones_captured4;
            massert(stone_diff == stone_diff2, "stone_diff1/2");
            massert(abs(stone_diff) - 1 == stones_captured4, "stone_diff/captur\
ed4");

            /*
            Test liberty counts for both players
            */
            for(move m = 0; m < TOTAL_BOARD_SIZ; ++m)
                if(b.p[m] == EMPTY && m != b.last_eaten){
                    u16 stones_captured1;
                    u8 l1 = libs_after_play_slow(&b, m, is_black,
                        &stones_captured1);
                    move stones_captured2;
                    u8 l2 = libs_after_play(&cb, m, is_black,
                        &stones_captured2);
                    bool stones_captured3;
                    u8 l3 = safe_to_play(&cb, m, is_black, &stones_captured3);
                    if(l1 != l2 || (l1 >= 2 && l3 != 2) || (l1 < 2 && l1 != l3))
                    {
                        printf("tested %s playing (%u), l1=%u l2=%u l3*=%u\n",
                            is_black ? "black" : "white", m, l1, l2, l3);
                        fprint_board(stdout, &b);
                        fprint_cfg_board(stdout, &cb);
                        exit(EXIT_FAILURE);
                    }
                    if((stones_captured3 && l3 == 0) || stones_captured1 !=
                        stones_captured2 || ((stones_captured1 > 0) !=
                            stones_captured3))
                    {
                        fprintf(stderr, "stones captured mismatch (1): (%u) %u \
%u %u\n", m, stones_captured1, stones_captured2, stones_captured3);
                        fprint_board(stdout, &b);
                        fprint_cfg_board(stdout, &cb);
                        exit(EXIT_FAILURE);
                    }
                }

            is_black = !is_black;

            for(move m = 0; m < TOTAL_BOARD_SIZ; ++m)
                if(b.p[m] == EMPTY && m != b.last_eaten)
                {
                    u16 stones_captured1;
                    u8 l1 = libs_after_play_slow(&b, m, is_black,
                        &stones_captured1);
                    move stones_captured2;
                    u8 l2 = libs_after_play(&cb, m, is_black,
                        &stones_captured2);
                    bool stones_captured3;
                    u8 l3 = safe_to_play(&cb, m, is_black, &stones_captured3);
                    if(l1 != l2 || (l1 >= 2 && l3 != 2) || (l1 < 2 && l1 != l3))
                    {
                        printf("tested %s playing (%u), l1=%u l2=%u l3*=%u\n",
                            is_black ? "black" : "white", m, l1, l2, l3);
                        fprint_board(stdout, &b);
                        fprint_cfg_board(stdout, &cb);
                        exit(EXIT_FAILURE);
                    }
                    if((stones_captured3 && l3 == 0) || stones_captured1 !=
                        stones_captured2 || ((stones_captured1 > 0) !=
                            stones_captured3))
                    {
                        fprintf(stderr, "stones captured mismatch (2): (%u) %u \
%u %u\n", m, stones_captured1, stones_captured2, stones_captured3);
                        fprint_board(stdout, &b);
                        fprint_cfg_board(stdout, &cb);
                        exit(EXIT_FAILURE);
                    }
                }
        }
        cfg_board_free(&cb);
    }

    u16 ignored;
    board l;
    clear_board(&l);
    attempt_play_slow(&l, coord_to_move(1, 0), true);
    attempt_play_slow(&l, coord_to_move(1, 1), true);
    u8 l1 = libs_after_play_slow(&l, coord_to_move(0, 0), false, &ignored);
    massert(l1 == 1, "libs_after_play_slow error");

    move ignored1;
    cfg_board sl;
    cfg_from_board(&sl, &l);
    l1 = libs_after_play(&sl, coord_to_move(0, 0), false, &ignored1);
    massert(l1 == 1, "libs_after_play");
    bool ignored2;
    l1 = safe_to_play(&sl, coord_to_move(0, 0), false, &ignored2);
    massert(l1 == 1, "safe_to_play");
    cfg_board_free(&sl);

    fprintf(stderr, " passed\n");
}

static void test_pattern(){
    fprintf(stderr, "%s: patterns...", _timestamp());
    u16 v1 = rand_u16(65535);
    u8 v[3][3];
    string_to_pat3(v, v1);
    u16 v2 = pat3_to_string((const u8(*)[3])v);
    massert(v1 == v2, "encoding/decoding 3x3 pattern");


    board b;
    clear_board(&b);
    cfg_board cb;
    cfg_board sb2;

    for(u32 tries = 0; tries < 50; ++tries){
        bool is_black = true;
        cfg_from_board(&cb, &b);
        for(move ki = 0; ki < TOTAL_BOARD_SIZ; ++ki)
        {
            move pl = rand_u16(TOTAL_BOARD_SIZ);

            if(can_play(&cb, pl, is_black))
                just_play(&cb, pl, is_black);
            else
                just_pass(&cb);

            for(move m = 0; m < TOTAL_BOARD_SIZ; ++m)
            {
                if(cb.p[m] != EMPTY)
                    continue;

                u16 hash_cfg = cb.hash[m];
                pat3_transpose(v, cb.p, m);
                u16 hash_pat3 = pat3_to_string((const u8(*)[3])v);
                massert(hash_cfg == hash_pat3,
                    "CFG from play and pat3 patterns 1");

                cfg_board_clone(&sb2, &cb);
                hash_cfg = sb2.hash[m];
                massert(hash_cfg == hash_pat3,
                    "CFG from board and pat3 patterns 2");
                cfg_board_free(&sb2);
            }

            is_black = !is_black;
        }

        cfg_board_free(&cb);
    }

    fprintf(stderr, " passed\n");
}

static void test_ladders()
{
    fprintf(stderr, "%s: tactical functions...", _timestamp());
    board b;
    cfg_board cb;

    /*
    Simplest ladder possible
    */
    clear_board(&b);
    b.p[coord_to_move(1, 0)] = BLACK_STONE;

    b.p[coord_to_move(0, 1)] = BLACK_STONE;
    b.p[coord_to_move(1, 1)] = WHITE_STONE;
    b.p[coord_to_move(2, 1)] = BLACK_STONE;

    /* This is not a ladder, just a stone in atari */
    cfg_from_board(&cb, &b);
    massert(!is_ladder(&cb, coord_to_move(1, 2), false), "ladder1");
    massert(can_be_killed(&cb, cb.g[coord_to_move(1, 1)]) ==
        coord_to_move(1, 2), "can_be_killed1");
    massert(can_be_saved(&cb, cb.g[coord_to_move(1, 1)]) == coord_to_move(1, 2),
        "can_be_saved1");
    cfg_board_free(&cb);

    b.p[coord_to_move(0, 2)] = BLACK_STONE;

    /* Now it's a ladder */
    cfg_from_board(&cb, &b);
    massert(is_ladder(&cb, coord_to_move(1, 2), false), "ladder2");
    massert(can_be_killed(&cb, cb.g[coord_to_move(1, 1)]) ==
        coord_to_move(1, 2), "can_be_killed2");
    massert(can_be_saved(&cb, cb.g[coord_to_move(1, 1)]) == NONE,
        "can_be_saved2");
    cfg_board_free(&cb);

    b.p[coord_to_move(6, 5)] = BLACK_STONE;
    /* Still a ladder */
    cfg_from_board(&cb, &b);
    massert(is_ladder(&cb, coord_to_move(1, 2), false), "ladder3");
    massert(can_be_killed(&cb, cb.g[coord_to_move(1, 1)]) ==
        coord_to_move(1, 2), "can_be_killed3");
    massert(can_be_saved(&cb, cb.g[coord_to_move(1, 1)]) == NONE,
        "can_be_saved3");
    cfg_board_free(&cb);

    b.p[coord_to_move(6, 5)] = WHITE_STONE;
    /* No longer a ladder because white can connect */
    cfg_from_board(&cb, &b);
    massert(!is_ladder(&cb, coord_to_move(1, 2), false), "ladder4");
    massert(can_be_killed(&cb, cb.g[coord_to_move(1, 1)]) ==
        coord_to_move(1, 2), "can_be_killed4");
    massert(can_be_saved(&cb, cb.g[coord_to_move(1, 1)]) == coord_to_move(1, 2),
        "can_be_saved4");
    cfg_board_free(&cb);

    /*
    Looks like a ladder but group can escape by capture
    */
    clear_board(&b);
    b.p[coord_to_move(2, 1)] = BLACK_STONE;
    b.p[coord_to_move(3, 1)] = BLACK_STONE;
    b.p[coord_to_move(4, 1)] = BLACK_STONE;

    b.p[coord_to_move(1, 2)] = BLACK_STONE;
    b.p[coord_to_move(2, 2)] = WHITE_STONE;
    b.p[coord_to_move(3, 2)] = WHITE_STONE;
    b.p[coord_to_move(4, 2)] = WHITE_STONE;
    b.p[coord_to_move(5, 2)] = BLACK_STONE;

    b.p[coord_to_move(2, 3)] = BLACK_STONE;
    b.p[coord_to_move(3, 3)] = WHITE_STONE;
    b.p[coord_to_move(4, 3)] = BLACK_STONE;

    b.p[coord_to_move(2, 4)] = BLACK_STONE;

    cfg_from_board(&cb, &b);
    massert(!is_ladder(&cb, coord_to_move(3, 4), false), "ladder5");
    massert(can_be_killed(&cb, cb.g[coord_to_move(3, 3)]) ==
        coord_to_move(3, 4), "can_be_killed5");
    massert(can_be_saved(&cb, cb.g[coord_to_move(3, 3)]) == coord_to_move(3, 4),
        "can_be_saved5");
    cfg_board_free(&cb);

    b.p[coord_to_move(5, 3)] = BLACK_STONE;

    /*
    Capture is now impossible so ladder is indeed a ladder
    */

    cfg_from_board(&cb, &b);
    massert(is_ladder(&cb, coord_to_move(3, 4), false), "ladder6");
    massert(can_be_killed(&cb, cb.g[coord_to_move(3, 3)]) ==
        coord_to_move(3, 4), "can_be_killed6");
    massert(can_be_saved(&cb, cb.g[coord_to_move(3, 3)]) == NONE,
        "can_be_saved6");
    cfg_board_free(&cb);


    /*
    Nakade
    */
    clear_board(&b);
    b.p[coord_to_move(0, 0)] = BLACK_STONE;
    b.p[coord_to_move(0, 1)] = BLACK_STONE;
    b.p[coord_to_move(0, 2)] = BLACK_STONE;
    b.p[coord_to_move(0, 3)] = BLACK_STONE;
    b.p[coord_to_move(0, 4)] = BLACK_STONE;
    b.p[coord_to_move(0, 5)] = WHITE_STONE;

    b.p[coord_to_move(1, 0)] = BLACK_STONE;
    b.p[coord_to_move(1, 4)] = BLACK_STONE;
    b.p[coord_to_move(1, 5)] = WHITE_STONE;

    b.p[coord_to_move(2, 0)] = BLACK_STONE;
    b.p[coord_to_move(2, 1)] = BLACK_STONE;
    b.p[coord_to_move(2, 2)] = BLACK_STONE;
    b.p[coord_to_move(2, 3)] = BLACK_STONE;
    b.p[coord_to_move(2, 4)] = BLACK_STONE;
    b.p[coord_to_move(2, 5)] = WHITE_STONE;

    b.p[coord_to_move(3, 0)] = WHITE_STONE;
    b.p[coord_to_move(3, 1)] = WHITE_STONE;
    b.p[coord_to_move(3, 2)] = WHITE_STONE;
    b.p[coord_to_move(3, 3)] = WHITE_STONE;
    b.p[coord_to_move(3, 4)] = WHITE_STONE;
    b.p[coord_to_move(3, 5)] = WHITE_STONE;

    cfg_from_board(&cb, &b);
    massert(can_be_killed(&cb, cb.g[coord_to_move(0, 0)]) ==
        coord_to_move(1, 2), "can_be_killed7");
    massert(can_be_saved(&cb, cb.g[coord_to_move(0, 0)]) == coord_to_move(1, 2),
        "can_be_saved7");
    cfg_board_free(&cb);

    fprintf(stderr, " passed\n");
}

static void test_board()
{
    fprintf(stderr, "%s: board reduction and operations...", _timestamp());
    for(u32 tes = 0; tes < 10000; ++tes){
        board b;
        clear_board(&b);

        bool is_black = true;
        for(u16 i = 0; i <= TOTAL_BOARD_SIZ / 2; ++i){
            move m = rand_u16(TOTAL_BOARD_SIZ);
            if(attempt_play_slow(&b, m, is_black))
                is_black = !is_black;
        }

        board b2;

        u8 packed[PACKED_BOARD_SIZ];
        pack_matrix(packed, b.p);
        unpack_matrix(b2.p, packed);

        massert(memcmp(b.p, b2.p, TOTAL_BOARD_SIZ) == 0,
            "packing/unpacking");


        memcpy(&b2, &b, sizeof(board));

        d8 reduction = reduce_auto(&b2, is_black);
        reduce_fixed(&b, reduction);

        /* fixed reduction works */
        massert(board_are_equal(&b2, &b), "fixed reduction");

        out_board ob;
        clear_out_board(&ob);
        random_play(&ob, &b, true); /* always as black */
        move m = select_play_fast(&ob);

        massert(b.p[m] == EMPTY, "busy intersection\n");
        just_play_slow(&b, m, true);
        reduction = reduce_auto(&b, true);
        /* b is now with one more play and reduced again */

        reduce_fixed(&b2, reduction);
        m = reduce_move(m, reduction);

        massert(b2.p[m] == EMPTY, "busy intersection\n");
        just_play_slow(&b2, m, true);
        massert(board_are_equal(&b, &b2), "play reduction");
    }
    fprintf(stderr, " passed\n");
}


#define SAMPLES 10000000

static u32 samples[SAMPLES];
static float samplesf[SAMPLES];

static void calc_distribution(
    u32 max
){
    printf("\ttarget average=%f\n", max == 0 ? 0.0 : ((double)(max - 1)) / 2.0);
    bool min_found = false;
    bool max_found = false;
    double avg = 0.0;
    for(u32 i = 0; i < SAMPLES; ++i)
    {
        avg += (double)samples[i];
        if(samples[i] == 0)
            min_found = true;
        if(samples[i] == max - 1)
            max_found = true;
        massert(max == 0 || samples[i] < max, "upper limit violation");
    }
    massert(min_found, "lower limit not found");
    massert(max == 0 || max_found, "upper limit not found");
    avg /= ((double)SAMPLES);
    printf("\taverage=%f\n", avg);
    double variance = 0.0;
    for(u32 i = 0; i < SAMPLES; ++i)
    {
        double m = ((double)samples[i]) - avg;
        variance += m * m;
    }
    variance /= ((double)SAMPLES);
    printf("\tvariance=%f\n", variance);
}

static void calc_distributionf(
    float max
){
    printf("\ttarget average=%f\n", max / 2.0);
    double avg = 0.0;
    for(u32 i = 0; i < SAMPLES; ++i)
    {
        avg += (double)samplesf[i];
        massert(samplesf[i] <= max, "upper limit violation");
    }
    avg /= ((double)SAMPLES);
    printf("\taverage=%f\n", avg);
    double variance = 0.0;
    for(u32 i = 0; i < SAMPLES; ++i)
    {
        double m = ((double)samplesf[i]) - avg;
        variance += m * m;
    }
    variance /= ((double)SAMPLES);
    printf("\tvariance=%f\n", variance);
}

static void test_rand_gen()
{
    fprintf(stderr, "%s: pseudo random generator...\n", _timestamp());

    printf("%s: rand_u16(0)\n", _timestamp());
    for(u32 i = 0; i < SAMPLES; ++i)
        samples[i] = rand_u16(0);
    calc_distribution(0);

    printf("%s: rand_u16(1)\n", _timestamp());
    for(u32 i = 0; i < SAMPLES; ++i)
        samples[i] = rand_u16(1);
    calc_distribution(1);

    printf("%s: rand_u16(7)\n", _timestamp());
    for(u32 i = 0; i < SAMPLES; ++i)
        samples[i] = rand_u16(7);
    calc_distribution(7);

    printf("%s: rand_u16(81)\n", _timestamp());
    for(u32 i = 0; i < SAMPLES; ++i)
        samples[i] = rand_u16(81);
    calc_distribution(81);

    printf("%s: rand_u16(100)\n", _timestamp());
    for(u32 i = 0; i < SAMPLES; ++i)
        samples[i] = rand_u16(100);
    calc_distribution(100);

    printf("%s: rand_u16(361)\n", _timestamp());
    for(u32 i = 0; i < SAMPLES; ++i)
        samples[i] = rand_u16(361);
    calc_distribution(361);

    printf("%s: rand_u32(0)\n", _timestamp());
    for(u32 i = 0; i < SAMPLES; ++i)
        samples[i] = rand_u32(0);
    calc_distribution(0);

    printf("%s: rand_u32(1)\n", _timestamp());
    for(u32 i = 0; i < SAMPLES; ++i)
        samples[i] = rand_u32(1);
    calc_distribution(1);

    printf("%s: rand_u32(8000)\n", _timestamp());
    for(u32 i = 0; i < SAMPLES; ++i)
        samples[i] = rand_u32(8000);
    calc_distribution(8000);

    printf("%s: rand_float(1)\n", _timestamp());
    for(u32 i = 0; i < SAMPLES; ++i)
    {
        samplesf[i] = rand_float(1.0);
        massert(samplesf[i] >= 0.0, "lower limit violation");
        massert(samplesf[i] < 1.0 + 0.0001, "upper limit violation");
    }
    calc_distributionf(1.0);

    printf("%s: rand_float(2.4)\n", _timestamp());
    for(u32 i = 0; i < SAMPLES; ++i)
    {
        samplesf[i] = rand_float(2.4);
        massert(samplesf[i] >= 0.0, "lower limit violation");
        massert(samplesf[i] < 2.4 + 0.0001, "upper limit violation");
    }
    calc_distributionf(2.4);
    printf("%s: test passed\n", _timestamp());
}

static void test_time_keeping()
{
    fprintf(stderr, "%s: time keeping...", _timestamp());

    u64 t = current_time_in_millis();
    sleep(1);
    u64 t2 = current_time_in_millis();
    massert(t2 >= t + 1000ULL, "lower limit violation");
    massert(t2 <= t + 1010ULL, "upper limit violation");

    fprintf(stderr, " passed\n");
}

static void test_zobrist_hashing()
{
    fprintf(stderr, "%s: zobrist hashing...", _timestamp());

    zobrist_init();
    board b;
    clear_board(&b);
    just_play_slow(&b, coord_to_move(1, 2), true);
    just_play_slow(&b, coord_to_move(2, 2), false);
    just_play_slow(&b, coord_to_move(2, 3), true);
    just_play_slow(&b, coord_to_move(2, 4), false);

    u64 hash1 = zobrist_new_hash(&b);
    u64 hash2 = hash1;

    out_board ob;
    clear_out_board(&ob);
    random_play(&ob, &b, true);
    move m = select_play_fast(&ob);
    zobrist_update_hash(&hash2, m, BLACK_STONE);
    hash1 = just_play_slow_and_get_hash(&b, m, true, hash1);
    u64 hash3 = zobrist_new_hash(&b);

    massert(hash1 == hash2, "hash mismatch 1");
    massert(hash1 == hash3, "hash mismatch 2");

    fprintf(stderr, " passed\n");
}

static void test_whole_game()
{
    fprintf(stderr, "%s: game record and MCTS...\n", _timestamp());

    out_board out_b;
    game_record gr;
    clear_game_record(&gr);
    bool last_passed = false;

    while(1)
    {
        board b;
        current_game_state(&b, &gr);
        bool is_black = current_player_color(&gr);
        opt_turn_maintenance(&b, is_black);

        u64 curr_time = current_time_in_millis();
        u64 stop_time = curr_time + 1000;
        u64 early_stop_time = curr_time + 800;

        bool has_play = evaluate_position(&b, is_black, &out_b, stop_time,
            early_stop_time);
        if(!has_play)
            break;
        move m = select_play(&out_b, is_black, &gr);
        massert(m == PASS || is_board_move(m), "illegal move format");
        if(m == PASS)
        {
            if(last_passed)
                break;
            last_passed = true;
        }else
            last_passed = false;

        add_play(&gr, m);
    }
    new_match_maintenance();

    fprintf(stderr, "%s: test passed\n", _timestamp());
}

int main(
    int argc,
    char * argv[]
){
    if(argc == 2 && strcmp(argv[1], "-version") == 0)
    {
        fprintf(stderr, "matilda %u.%u\n", VERSION_MAJOR, VERSION_MINOR);
        return 0;
    }
    else
        if(argc > 1)
        {
            fprintf(stderr, "usage: %s [-version]\n", argv[0]);
            return 0;
        }

    alloc_init();
    config_logging(LOG_CRITICAL | LOG_WARNING | LOG_INFORMATION);
    assert_data_folder_exists();
    rand_init();
    cfg_board_init();
    zobrist_init();

#if 1
    omp_set_num_threads(1);
#endif

    if(1){
        test_pattern();
        test_board();
        test_cfg_board();
        test_ladders();
        test_rand_gen();
        test_time_keeping();
        test_zobrist_hashing();
        test_whole_game();
    }else
        while(1)
            test_whole_game();

    return 0;
}
