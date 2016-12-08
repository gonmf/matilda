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
#include "mcts.h"
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
extern bool pass_when_losing;
extern u32 limit_by_playouts;

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
        fprintf(stderr, "Play is malformed.\n");
        return true;
    }

    board current_state;
    current_game_state(&current_state, &current_game);
    if(!can_play_slow(&current_state, is_black, m))
    {
        fprintf(stderr, "Play is illegal.\n");
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
    bool has_play;
    if(limit_by_playouts > 0)
        has_play = evaluate_position_sims(&current_state, is_black, &out_b,
            limit_by_playouts);
    else
        has_play = evaluate_position_timed(&current_state, is_black, &out_b,
            stop_time, early_stop_time);

    if(!has_play)
    {
        if(pass_when_losing)
            *resigned = true;
        else
            *passed = true;
        return;
    }

    move m;
    if(out_b.pass >= JUST_PASS_WINRATE)
        m = PASS;
    else
        m = select_play(&out_b, is_black, &current_game);

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
            fprintf(stderr, "Game record written to %s.\n", filename);
        else
            fprintf(stderr, "Error encountered when attempting to write game re\
cord to file.\n");
    }

    fprintf(stderr,
        "Start new game?\nY - Yes\nN - No (quit)\nS - Yes but switch colors\n");
    while(1)
    {
        fprintf(stderr, ">");
        fflush(stderr);
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
    fprintf(stderr, "Game result: %s\n", s);
    release(s);
}

void main_text(bool is_black){
    fclose(stdout);

    flog_info("gtp", "matilda now running over text interface");
    char * s = alloc();
    build_info(s);
    flog_info("gtp", s);

    komi_to_string(s, DEFAULT_KOMI);
    fprintf(stderr, "Running in text mode. In this mode the options are limited\
 and no time limit is\nenforced. To run using GTP add the flag --mode gtp. Play\
ing with Chinese rules\nwith %s komi; the game is over after two passes or a re\
signation.\n\n", s);
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

        board current_state;
        current_game_state(&current_state, &current_game);

        if(current_state.last_played == NONE)
        {
            fprintf(stderr, "\n\"Have a good game.\"\n");
        }

        fprintf(stderr, "\n");
        fprint_game_record(stderr, &current_game);
        fprintf(stderr, "\n");
        fprint_board(stderr, &current_state);
        fprintf(stderr, "\n");

        /*
        Computer turn
        */
        if(is_black != human_player_color)
        {
            fprintf(stderr, "Computer thinking...\n");
            text_genmove(is_black, &passed, &resigned);
            fprintf(stderr, "\n");

            if(resigned)
            {
                current_game.finished = true;
                current_game.resignation = true;
                fprintf(stderr, "\n\"I resign. Thank you for the game.\"\n\n");

                fprintf(stderr, "%s (%c) wins by resignation.\n\n", is_black ?
                    "White" : "Black", is_black ? WHITE_STONE_CHAR :
                    BLACK_STONE_CHAR);
                text_newgame(&human_player_color, &is_black);
                continue;
            }

            if(passed)
            {
                if(last_played_pass)
                {
                    current_game.finished = true;
                    fprintf(stderr, "Computer passes, game is over.\n");
                    text_print_score();
                    fprintf(stderr, "\n");
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
            fprintf(stderr, "(Type the board position, like %s, or undo/pass/re\
sign/tip/score/quit)\n", mstr);
            release(mstr);
        }
        while(1)
        {
            fprintf(stderr, "Your turn (%c): ", is_black ? BLACK_STONE_CHAR :
                WHITE_STONE_CHAR);
            fflush(stderr);

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
                current_game.finished = true;
                current_game.resignation = true;
                fprintf(stderr, "%s (%c) wins by resignation.\n\n", is_black ?
                    "White" : "Black", is_black ? WHITE_STONE_CHAR :
                    BLACK_STONE_CHAR);
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
                fprintf(stderr, "Type the board position, like %s, or undo/pass\
/resign/score/quit\n\n", mstr);
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
                    fprintf(stderr, "%s", buffer);
                    release(buffer);
                    --tips;
                }

                if(tips == 0)
                    fprintf(stderr, "You have no tips left.\n");
                else
                    fprintf(stderr, "You now have %u/3 tips left.\n", tips);
                continue;
            }

            if(strcmp(line, "score") == 0)
            {
                current_game_state(&current_state, &current_game);
                d16 score = score_stones_and_area(current_state.p);
                char * s = alloc();
                score_to_string(s, score);
                fprintf(stderr, "Score estimate with %s to play: %s\n\n",
                    is_black ? "black" : "white", s);
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
                    current_game.finished = true;
                    fprintf(stderr, "Two passes in a row, game is over.\n");
                    text_print_score();
                    fprintf(stderr, "\n");
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

