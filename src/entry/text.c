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

#include "board.h"
#include "engine.h"
#include "flog.h"
#include "game_record.h"
#include "scoring.h"
#include "state_changes.h"
#include "stringm.h"
#include "analysis.h"
#include "frisbee.h"
#include "sgf.h"
#include "timem.h"
#include "time_ctrl.h"
#include "buffer.h"
#include "pts_file.h"

extern game_record current_game;
extern time_system current_clock_black;
extern time_system current_clock_white;
extern bool save_all_games_to_file;

extern bool estimate_score;
extern float frisbee_prob;

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

    move m;
#if EUROPEAN_NOTATION
    m = coord_parse_alpha_num(vertex);
#else
    m = coord_parse_num_num(vertex);
#endif

    if(m == NONE)
    {
        printf("Play is malformed.\n");
        return true;
    }

    board * current_state = current_game_state(&current_game);
    if(!can_play_slow(current_state, m, is_black))
    {
        printf("Play is illegal.\n");
        return true;
    }

    if(ENABLE_FRISBEE_GO && frisbee_prob < 1.0)
    {
        move n = frisbee_divert_play(current_state, is_black, m, frisbee_prob);
        if(m != n)
        {
            if(superko_violation(&current_game, is_black, n))
                n = NONE;

            if(n == NONE)
                printf("Player attempted to play %s but it ended an illegal \
play instead.\n", coord_to_alpha_num(m));
            else
            {
                printf("Player attempted to play %s ", coord_to_alpha_num(m));
                printf("but it landed on %s instead.\n", coord_to_alpha_num(n));
            }
            m = n;
        }
    }

    add_play(&current_game, m);
    opt_turn_maintenance(current_state, !is_black);
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
    board * current_state = current_game_state(&current_game);

    u16 stones = stone_count(current_state->p);
    u32 milliseconds;
    if(is_black)
        milliseconds = calc_time_to_play(&current_clock_black, stones);
    else
        milliseconds = calc_time_to_play(&current_clock_white, stones);

    u64 curr_time = current_time_in_millis();
    u64 stop_time = curr_time + milliseconds;
    u64 early_stop_time = curr_time + (milliseconds / 4);
    bool has_play = evaluate_position(current_state, is_black, &out_b,
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

    if(ENABLE_FRISBEE_GO && frisbee_prob < 1.0)
    {
        current_state = current_game_state(&current_game);
        move n = frisbee_divert_play(current_state, is_black, m, frisbee_prob);
        if(m != n)
        {
            if(superko_violation(&current_game, is_black, n))
                n = NONE;

            if(n == NONE)
                printf("Matilda attempted to play %s but it ended an illegal \
play instead.\n", coord_to_alpha_num(m));
            else
            {
                printf("Matilda attempted to play %s ", coord_to_alpha_num(m));
                printf("but it landed on %s instead.\n", coord_to_alpha_num(n));
            }
            m = n;
        }
    }

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
            printf("Error encountered when attempting to write game record to \
file.\n");
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

static void text_print_score(bool is_black)
{
    board * current_state = current_game_state(&current_game);
    s16 score;
    if(estimate_score)
    {
        if(stone_count(current_state->p) > BOARD_SIZ * BOARD_SIZ / 2)
            score = score_estimate(current_state, is_black);
        else
            score = score_stones_and_area(current_state->p);
    }else
        score = 0;

    printf("Game result: %s\n", score_to_string(score));
}

void main_text(bool is_black){
    printf("Matilda %u.%u running in text mode. In this mode the options are \
limited and no time limit is enforced. To run using GTP add the flag -gtp. \
Playing with Chinese rules with %s komi; game is over after two passes or a \
resignation.\n\n", VERSION_MAJOR, VERSION_MINOR, komi_to_string(DEFAULT_KOMI));

    if(ENABLE_FRISBEE_GO && frisbee_prob < 1.0)
        printf("Frisbee Go variant is active. Each board play has a %u%% \
chance of missing.\n", (u32)(frisbee_prob * 100.0));

    bool human_player_color = is_black;


    is_black = true;
    bool first_interactive_play = true;
    bool passed;
    bool resigned;
    bool last_played_pass = false;

    load_hoshi_points();

    clear_game_record(&current_game);
    update_names(human_player_color);

    char * buf = get_buffer();
    while(1)
    {
        passed = false;
        resigned = false;

        printf("\n");
        fprint_game_record(stdout, &current_game);
        printf("\n");
        board * current_state = current_game_state(&current_game);
        fprint_board(stdout, current_state);
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
                    text_print_score(!is_black);
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
            printf("(Type the board position, like %s, or \
undo/pass/resign/tip/score/quit)\n", EUROPEAN_NOTATION ?
                coord_to_alpha_num(coord_to_move(3, 3)) :
                coord_to_num_num(coord_to_move(3, 3)));
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

            flog_prot(line);
            flog_prot("\n");

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
                printf("Type the board position, like %s, or \
undo/pass/resign/score/quit\n\n", EUROPEAN_NOTATION ?
                    coord_to_alpha_num(coord_to_move(3, 3)) :
                    coord_to_num_num(coord_to_move(3, 3)));
                continue;
            }

            if(strcmp(line, "tip") == 0)
            {
                if(tips > 0)
                {
                    board * current_state = current_game_state(&current_game);
                    char * buffer = get_buffer();
                    request_opinion(buffer, current_state, is_black, 1000);
                    printf("%s", buffer);
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
                board * current_state = current_game_state(&current_game);
                s16 score = estimate_score ? score_estimate(current_state,
                    is_black) : 0;
                printf("Score estimate with %s to play: %s\n\n", is_black ?
                    "black" : "white", score_to_string(score));
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
                    text_print_score(!is_black);
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

