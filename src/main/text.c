/*
Matilda application with text interface

The functionality is very limited in text mode; this is really just a fallback
for systems without a graphical program.

The commands supported are: quit, resign, undo and specifying plays (coordinates
or pass).
*/

#include "matilda.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "alloc.h"
#include "analysis.h"
#include "board.h"
#include "engine.h"
#include "flog.h"
#include "game_record.h"
#include "pts_file.h"
#include "scoring.h"
#include "sgf.h"
#include "state_changes.h"
#include "stringm.h"
#include "time_ctrl.h"
#include "timem.h"
#include "version.h"

extern game_record current_game;
extern time_system current_clock_black;
extern time_system current_clock_white;
extern bool save_all_games_to_file;

static u8 tips = 3;

static void update_names(
    bool human_is_black
){
    if(human_is_black)
    {
        snprintf(current_game.black_name, MAX_PLAYER_NAME_SIZ, "human");
        snprintf(current_game.white_name, MAX_PLAYER_NAME_SIZ, "matilda");
    }
    else
    {
        snprintf(current_game.black_name, MAX_PLAYER_NAME_SIZ, "matilda");
        snprintf(current_game.white_name, MAX_PLAYER_NAME_SIZ, "human");
    }
}

/*
RETURNS whether the input is malformed
*/
static bool text_play(
    const char * vertex,
    bool is_black,
    bool * passed
){
    if(strcmp(vertex, "pass") == 0)
    {
        add_play(&current_game, PASS);
        *passed = true;
        return false;
    }

#if EUROPEAN_NOTATION
    move m = coord_parse_alpha_num(vertex);
#else
    move m = coord_parse_num_num(vertex);
#endif

    if(m == NONE)
    {
        printf("Play is malformed.\n");
        return true;
    }

    board current_state;
    current_game_state(&current_state, &current_game);
    if(!can_play_slow(&current_state, is_black, m))
    {
        printf("Play is illegal.\n");
        return true;
    }

    add_play(&current_game, m);
    opt_turn_maintenance(&current_state, !is_black);
    *passed = false;
    return false;
}



/*
Simple function selecting the next play in text mode.
*/
static void text_genmove(
    bool is_black,
    bool * passed,
    bool * resigned
){
    out_board out_b;
    board current_state;
    current_game_state(&current_state, &current_game);

    u16 stones = stone_count(current_state.p);
    u32 milliseconds;
    if(is_black)
        milliseconds = calc_time_to_play(&current_clock_black, stones);
    else
        milliseconds = calc_time_to_play(&current_clock_white, stones);

    u64 curr_time = current_time_in_millis();
    u64 stop_time = curr_time + milliseconds;
    u64 early_stop_time = curr_time + (milliseconds / 4);
    bool has_play = evaluate_position(&current_state, is_black, &out_b,
        stop_time, early_stop_time);

    if(!has_play)
    {
#if CAN_RESIGN
        *resigned = true;
        return;
#else
        *resigned = false;
#endif
        *passed = true;
        return;
    }

    move m = select_play(&out_b, is_black, &current_game);

    add_play(&current_game, m);

    *passed = (m == PASS);
}

static void text_newgame(
    bool * human_player_color,
    bool * is_black
){
    if(save_all_games_to_file && current_game.turns > 0)
    {
        char filename[32];
        if(export_game_as_sgf_auto_named(&current_game, filename))
            printf("Game record written to %s.\n", filename);
        else
            printf("Error encountered when attempting to write game record to f\
ile.\n");
    }

    printf("Start new game?\nY - Yes\nN - No (quit)\nS - Switch colors\n");
    while(1)
    {
        printf(">");
        fflush(stdout);
        char c = getchar();
        if(c == 'y' || c == 'Y')
        {
            getchar();
            *is_black = true;
            clear_game_record(&current_game);
            new_match_maintenance();
            update_names(*human_player_color);
            tips = 3;
            return;
        }
        if(c == 'n' || c == 'N')
        {
            getchar();
            exit(EXIT_SUCCESS);
        }
        if(c == 's' || c == 'S')
        {
            getchar();
            *human_player_color = !(*human_player_color);
            *is_black = true;
            clear_game_record(&current_game);
            update_names(*human_player_color);
            new_match_maintenance();
            tips = 3;
            return;
        }
    }

}

static void text_print_score()
{
    board current_state;
    current_game_state(&current_state, &current_game);
    d16 score = score_stones_and_area(current_state.p);

    char * s = alloc();
    score_to_string(s, score);
    printf("Game result: %s\n", s);
    release(s);
}

void main_text(bool is_black){
    flog_set_print_to_stderr(false);
    flog_info("gtp", "matilda now running over text interface");
    char * s = alloc();
    build_info(s);
    flog_info("gtp", s);

    komi_to_string(s, DEFAULT_KOMI);
    printf("Running in text mode. In this mode the options are limited and no \
time limit is enforced. To run using GTP add the flag -gtp. Playing with Chines\
e rules with %s komi; game is over after two passes or a resignation.\n\n", s);
    release(s);

    bool human_player_color = is_black;


    is_black = true;
    bool first_interactive_play = true;
    bool passed;
    bool resigned;
    bool last_played_pass = false;

    load_hoshi_points();

    clear_game_record(&current_game);
    update_names(human_player_color);

    char * buf = alloc();
    while(1)
    {
        passed = false;
        resigned = false;

        printf("\n");
        fprint_game_record(stdout, &current_game);
        printf("\n");
        board current_state;
        current_game_state(&current_state, &current_game);
        fprint_board(stdout, &current_state);
        printf("\n");

        /*
        Computer turn
        */
        if(is_black != human_player_color)
        {
            printf("Computer thinking...\n");
            text_genmove(is_black, &passed, &resigned);
            printf("\n");

            if(resigned)
            {
                printf("%s (%c) wins by resignation.\n\n", is_black ? "White" :
                    "Black", is_black ? WHITE_STONE_CHAR : BLACK_STONE_CHAR);
                text_newgame(&human_player_color, &is_black);
                continue;
            }

            if(passed)
            {
                if(last_played_pass)
                {
                    printf("Computer passes, game is over.\n");
                    text_print_score();
                    printf("\n");
                    last_played_pass = false;
                    text_newgame(&human_player_color, &is_black);
                    continue;
                }
                else
                    last_played_pass = true;
            }
            else
                last_played_pass = false;

            is_black = !is_black;
            continue;
        }

        /*
        Human turn
        */
        if(first_interactive_play)
        {
            first_interactive_play = false;
            char * mstr = alloc();
#if EUROPEAN_NOTATION
            coord_to_alpha_num(mstr, coord_to_move(3, 3));
#else
            coord_to_num_num(mstr, coord_to_move(3, 3));
#endif
            printf("(Type the board position, like %s, or undo/pass/resign/tip/\
score/quit)\n", mstr);
            release(mstr);
        }
        while(1)
        {
            printf("Your turn (%c): ", is_black ? BLACK_STONE_CHAR :
                WHITE_STONE_CHAR);
            fflush(stdout);

            char * line = fgets(buf, MAX_PAGE_SIZ, stdin);
            if(line == NULL)
                continue;

            line = trim(buf);
            if(line == NULL)
                continue;

            lower_case(line);

            flog_prot("text", line);

            if(strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0)
                exit(EXIT_SUCCESS);

            if(strcmp(line, "resign") == 0)
            {
                printf("%s (%c) wins by resignation.\n\n", is_black ? "White" :
                    "Black", is_black ? WHITE_STONE_CHAR : BLACK_STONE_CHAR);
                text_newgame(&human_player_color, &is_black);
                break;
            }

            if(strcmp(line, "help") == 0)
            {
                char * mstr = alloc();
#if EUROPEAN_NOTATION
                coord_to_alpha_num(mstr, coord_to_move(3, 3));
#else
                coord_to_num_num(mstr, coord_to_move(3, 3));
#endif
                printf("Type the board position, like %s, or undo/pass/resign/s\
core/quit\n\n", mstr);
                release(mstr);
                continue;
            }

            if(strcmp(line, "tip") == 0)
            {
                if(tips > 0)
                {
                    current_game_state(&current_state, &current_game);
                    char * buffer = alloc();
                    request_opinion(buffer, &current_state, is_black, 1000);
                    printf("%s", buffer);
                    release(buffer);
                    --tips;
                }

                if(tips == 0)
                    printf("You have no tips left.\n");
                else
                    printf("You now have %u/3 tips left.\n", tips);
                continue;
            }

            if(strcmp(line, "score") == 0)
            {
                current_game_state(&current_state, &current_game);
                d16 score = score_stones_and_area(current_state.p);
                char * s = alloc();
                score_to_string(s, score);
                printf("Score estimate with %s to play: %s\n\n", is_black ?
                    "black" : "white", s);
                release(s);
                continue;
            }

            if(strcmp(line, "undo") == 0)
            {
                if(undo_last_play(&current_game))
                {
                    is_black = !is_black;
                    if(undo_last_play(&current_game))
                        is_black = !is_black;
                }
                break;
            }

            if(text_play(line, is_black, &passed))
                continue; /* Malformed command */

            if(passed)
            {
                if(last_played_pass)
                {
                    printf("Two passes in a row, game is over.\n");
                    text_print_score();
                    printf("\n");
                    text_newgame(&human_player_color, &is_black);
                }else
                    last_played_pass = true;
            }else
                last_played_pass = false;

            is_black = !is_black;
            break;
        }
    }
}

