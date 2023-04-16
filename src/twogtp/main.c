/*
Simple "twogtp" implementation - a program that pits two GTP-speaking programs
against each other, using a third one as referee in case of game end by
consecutive draws.
Please see the example scritps in the root folder twogtp/ on how to use this.
*/

#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

static void open_program(const char * command, int cpipe[]) {
  int parent_to_child[2], child_to_parent[2];

  pipe(parent_to_child);
  pipe(child_to_parent);
  pid_t pid = fork();

  if (pid == 0) { // child process
    close(parent_to_child[1]);
    dup2(parent_to_child[0], STDIN_FILENO);
    close(child_to_parent[0]);
    dup2(child_to_parent[1], STDOUT_FILENO);
    execl("/bin/sh", "sh", "-c", command, NULL);
    perror("execl");
    exit(EXIT_FAILURE);
  }

  close(parent_to_child[0]);
  close(child_to_parent[1]);

  cpipe[0] = child_to_parent[0];
  cpipe[1] = parent_to_child[1];
  cpipe[2] = pid;
}

static void send_command(const int cpipe[2], const char * message, char * response_buffer) {
  char buf[1024];
  snprintf(buf, 1024, "%s\n", message);
  write(cpipe[1], buf, strlen(buf));

  if (response_buffer == NULL) {
    response_buffer = buf;
  }

  int rtotal = 0;
  while (1) {
    int r = read(cpipe[0], response_buffer + rtotal, 1024 - rtotal);
    if (r > 0) {
      rtotal += r;
      if (rtotal >= 2) {
        if (response_buffer[rtotal - 2] == '\n' && response_buffer[rtotal - 1] == '\n') {
          response_buffer[rtotal - 2] = 0;
          break;
        }
      }
    }

    usleep(50000);
  }
}

static int play_game(
  int black_player_pipe[2],
  int white_player_pipe[2],
  int referee_pipe[2],
  int board_size,
  int komi
) {
  char buffer[1024];
  char buffer2[1060];

  snprintf(buffer2, 1060, "boardsize %d", board_size);
  send_command(black_player_pipe, buffer2, buffer);
  send_command(white_player_pipe, buffer2, buffer);
  send_command(referee_pipe, buffer2, buffer);
  if ((komi % 2) == 1) {
    snprintf(buffer2, 1060, "komi %d.5", komi / 2);
  } else {
    snprintf(buffer2, 1060, "komi %d", komi / 2);
  }
  send_command(black_player_pipe, buffer2, buffer);
  send_command(white_player_pipe, buffer2, buffer);
  send_command(referee_pipe, buffer2, buffer);
  send_command(black_player_pipe, "clear_board", buffer);
  send_command(white_player_pipe, "clear_board", buffer);
  send_command(referee_pipe, "clear_board", buffer);

  int last_move_pass = 0;

  while (1) {
    send_command(black_player_pipe, "genmove black", buffer);

    if (strcmp(buffer + 2, "resign") == 0) {
      return -1;
    }
    if (strcmp(buffer + 2, "pass") == 0) {
      if (last_move_pass) {
        send_command(referee_pipe, "play black pass", buffer);
        break;
      } else {
        last_move_pass = 1;
      }
    } else {
      last_move_pass = 0;
    }
    snprintf(buffer2, 1060, "play black %s", buffer + 2);
    send_command(white_player_pipe, buffer2, buffer);
    send_command(referee_pipe, buffer2, buffer);

    send_command(white_player_pipe, "genmove white", buffer);

    if (strcmp(buffer + 2, "resign") == 0) {
      return 1;
    }
    if (strcmp(buffer + 2, "pass") == 0) {
      if (last_move_pass) {
        send_command(referee_pipe, "play white pass", buffer);
        break;
      } else {
        last_move_pass = 1;
      }
    } else {
      last_move_pass = 0;
    }
    snprintf(buffer2, 1060, "play white %s", buffer + 2);
    send_command(black_player_pipe, buffer2, buffer);
    send_command(referee_pipe, buffer2, buffer);
  }

  send_command(referee_pipe, "final_score", buffer);
  if (buffer[2] == 'B' || buffer[2] == 'b') {
    return 1;
  }
  if (buffer[2] == 'W' || buffer[2] == 'w') {
    return -1;
  }
  return 0;
}

int main(int argc, char * argv[]) {
  char * black_player = NULL;
  char * white_player = NULL;
  char * referee = NULL;
  int board_size = 19;
  int komi = 15;
  int alternate = 0;
  int games = 1;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--white") == 0 && i < argc - 1) {
      white_player = argv[i + 1];
      i++;
      continue;
    }
    if (strcmp(argv[i], "--black") == 0 && i < argc - 1) {
      black_player = argv[i + 1];
      i++;
      continue;
    }
    if (strcmp(argv[i], "--referee") == 0 && i < argc - 1) {
      referee = argv[i + 1];
      i++;
      continue;
    }
    if (strcmp(argv[i], "--games") == 0 && i < argc - 1) {
      games = atoi(argv[i + 1]);
      if (games < 1) {
        fprintf(stderr, "Illegal games count\n");
        exit(EXIT_FAILURE);
      }
      i++;
      continue;
    }
    if (strcmp(argv[i], "--size") == 0 && i < argc - 1) {
      board_size = atoi(argv[i + 1]);
      if (board_size < 5) {
        fprintf(stderr, "Illegal board size\n");
        exit(EXIT_FAILURE);
      }
      i++;
      continue;
    }
    if (strcmp(argv[i], "--komi") == 0 && i < argc - 1) {
      komi = (int)(atof(argv[i + 1]) * 2);
      if (komi < 0) {
        fprintf(stderr, "Illegal komi\n");
        exit(EXIT_FAILURE);
      }
      i++;
      continue;
    }
    if (strcmp(argv[i], "--alternate") == 0) {
      alternate = 1;
      continue;
    }
    fprintf(stderr, "Unknown argument %s\n", argv[i]);
    exit(EXIT_FAILURE);
  }

  if (!black_player) {
    fprintf(stderr, "Black player not given\n");
    exit(EXIT_FAILURE);
  }

  if (!white_player) {
    fprintf(stderr, "White player not given\n");
    exit(EXIT_FAILURE);
  }

  if (!referee) {
    fprintf(stderr, "Referee program not given\n");
    exit(EXIT_FAILURE);
  }

  int black_player_pipe[3];
  int white_player_pipe[3];
  int referee_pipe[3];

  open_program(black_player, black_player_pipe);
  open_program(white_player, white_player_pipe);
  open_program(referee, referee_pipe);
  send_command(black_player_pipe, "version", NULL);
  send_command(white_player_pipe, "version", NULL);
  send_command(referee_pipe, "version", NULL);

  int wins = 0;
  int draws = 0;
  int losses = 0;

  for (int game = 0; game < games; ++game) {
    printf("Starting game %d/%d\n", game + 1, games);

    int victor;
    if (alternate && (game % 2) == 1) {
      victor = play_game(
        white_player_pipe,
        black_player_pipe,
        referee_pipe,
        board_size,
        komi
      );

      if (victor == 1) {
        losses += 1;
        printf("Player B wins (playing as black)\n");
      } else if (victor == -1) {
        wins += 1;
        printf("Player A wins (playing as white)\n");
      } else {
        draws += 1;
        printf("Draw with player A as white.\n");
      }
    } else {
      victor = play_game(
        black_player_pipe,
        white_player_pipe,
        referee_pipe,
        board_size,
        komi
      );

      if (victor == 1) {
        wins += 1;
        printf("Player A wins (playing as black)\n");
      } else if (victor == -1) {
        losses += 1;
        printf("Player B wins (playing as white)\n");
      } else {
        draws += 1;
        printf("Draw with player A as black.\n");
      }
    }
  }

  send_command(black_player_pipe, "quit", NULL);
  send_command(white_player_pipe, "quit", NULL);
  send_command(referee_pipe, "quit", NULL);

  kill(black_player_pipe[2], SIGKILL);
  kill(white_player_pipe[2], SIGKILL);
  kill(referee_pipe[2], SIGKILL);

  printf("Finished.\n");
  printf("Player A wins/draws/losses: %d / %d / %d\n", wins, draws, losses);

  return EXIT_SUCCESS;
}
