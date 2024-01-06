#include "enums.h"
#include "macros.h"
#include "nnue/nnue.h"
#include "quanticade.h"
#include "structs.h"
#include "utils.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

pthread_t main_search_thread;

#define start_position                                                         \
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 "

/**********************************\
 ==================================

                UCI
          forked from VICE
         by Richard Allbert

 ==================================
\**********************************/
// TODO REDO entire UCI
//  parse user/GUI move string input (e.g. "e7e8q")
static int parse_move(engine_t *engine, char *move_string) {
  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(engine, move_list);

  // parse source square
  int source_square = (move_string[0] - 'a') + (8 - (move_string[1] - '0')) * 8;

  // parse target square
  int target_square = (move_string[2] - 'a') + (8 - (move_string[3] - '0')) * 8;

  // loop over the moves within a move list
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // init move
    int move = move_list->moves[move_count];

    // make sure source & target squares are available within the generated move
    if (source_square == get_move_source(move) &&
        target_square == get_move_target(move)) {
      // init promoted piece
      int promoted_piece = get_move_promoted(move);

      // promoted piece is available
      if (promoted_piece) {
        // promoted to queen
        if ((promoted_piece == Q || promoted_piece == q) &&
            move_string[4] == 'q')
          // return legal move
          return move;

        // promoted to rook
        else if ((promoted_piece == R || promoted_piece == r) &&
                 move_string[4] == 'r')
          // return legal move
          return move;

        // promoted to bishop
        else if ((promoted_piece == B || promoted_piece == b) &&
                 move_string[4] == 'b')
          // return legal move
          return move;

        // promoted to knight
        else if ((promoted_piece == N || promoted_piece == n) &&
                 move_string[4] == 'n')
          // return legal move
          return move;

        // continue the loop on possible wrong promotions (e.g. "e7e8f")
        continue;
      }

      // return legal move
      return move;
    }
  }

  // return illegal move
  return 0;
}

// parse UCI "position" command
static void parse_position(engine_t *engine, char *command) {
  // shift pointer to the right where next token begins
  command += 9;

  // init pointer to the current character in the command string
  char *current_char = command;

  // parse UCI "startpos" command
  if (strncmp(command, "startpos", 8) == 0)
    // init chess board with start position
    parse_fen(engine, start_position);

  // parse UCI "fen" command
  else {
    // make sure "fen" command is available within command string
    current_char = strstr(command, "fen");

    // if no "fen" command is available within command string
    if (current_char == NULL)
      // init chess board with start position
      parse_fen(engine, start_position);

    // found "fen" substring
    else {
      // shift pointer to the right where next token begins
      current_char += 4;

      // init chess board with position from FEN string
      parse_fen(engine, current_char);
    }
  }

  // parse moves after position
  current_char = strstr(command, "moves");

  // moves available
  if (current_char != NULL) {
    // shift pointer to the right where next token begins
    current_char += 6;

    // loop over moves within a move string
    while (*current_char) {
      // parse next move
      int move = parse_move(engine, current_char);

      // if no more moves
      if (move == 0)
        // break out of the loop
        break;

      // increment repetition index
      engine->repetition_index++;

      // write hash key into a repetition table
      engine->repetition_table[engine->repetition_index] =
          engine->board.hash_key;

      // make move on the chess board
      make_move(engine, move, all_moves);

      // move current character pointer to the end of current move
      while (*current_char && *current_char != ' ')
        current_char++;

      // go to the next move
      current_char++;
    }
  }
}

void *search_position_thread(void *data) {
  search_data_t *search_data = (search_data_t *)data;
  engine_t *engine = malloc(sizeof(engine_t));
  memcpy(engine, search_data->engine, sizeof(engine_t));
  search_position(engine, search_data->search_info, search_data->hash_table,
                  search_data->depth);
  free(engine);
  return 0;
}

pthread_t launch_search_thread(engine_t *engine, search_info_t *search_info,
                               tt_t *hash_table, int depth) {
  search_data_t search_data = {engine, search_info, hash_table, depth};
  search_data.engine = engine;
  search_data.search_info = search_info;
  search_data.hash_table = hash_table;
  search_data.depth = depth;
  pthread_t search_thread;
  pthread_create(&search_thread, NULL, search_position_thread,
                 (void *)&search_data);
  return search_thread;
}

static void parse_go(engine_t *engine, search_info_t *search_info,
                     tt_t *hash_table, char *command) {
  // reset time control
  reset_time_control(engine);

  // init parameters
  int depth = -1;

  // init argument
  char *argument = NULL;

  // infinite search
  if ((argument = strstr(command, "infinite"))) {
  }

  // match UCI "binc" command
  if ((argument = strstr(command, "binc")) && engine->board.side == black)
    // parse black time increment
    search_info->inc = atoi(argument + 5);

  // match UCI "winc" command
  if ((argument = strstr(command, "winc")) && engine->board.side == white)
    // parse white time increment
    search_info->inc = atoi(argument + 5);

  // match UCI "wtime" command
  if ((argument = strstr(command, "wtime")) && engine->board.side == white)
    // parse white time limit
    search_info->time = atoi(argument + 6);

  // match UCI "btime" command
  if ((argument = strstr(command, "btime")) && engine->board.side == black)
    // parse black time limit
    search_info->time = atoi(argument + 6);

  // match UCI "movestogo" command
  if ((argument = strstr(command, "movestogo")))
    // parse number of moves to go
    search_info->movestogo = atoi(argument + 10);

  // match UCI "movetime" command
  if ((argument = strstr(command, "movetime"))) {
    // parse amount of time allowed to spend to make a move
    search_info->time = atoi(argument + 9);
    search_info->movestogo = 1;
  }

  // match UCI "depth" command
  if ((argument = strstr(command, "depth")))
    // parse search depth
    depth = atoi(argument + 6);

  // init start time
  search_info->starttime = get_time_ms();

  // if time control is available
  if (search_info->time != -1) {
    // flag we're playing with time control
    search_info->timeset = 1;

    // set up timing
    search_info->time /= search_info->movestogo;

    // lag compensation
    search_info->time -= 50;

    // if time is up
    if (search_info->time < 0) {
      // restore negative time to 0
      search_info->time = 0;

      // inc lag compensation on 0+inc time controls
      search_info->inc -= 50;

      // timing for 0 seconds left and no inc
      if (search_info->inc < 0)
        search_info->inc = 1;
    }

    // init stoptime
    search_info->stoptime =
        search_info->starttime + search_info->time + search_info->inc;
  }

  // if depth is not available
  if (depth == -1)
    // set depth to 64 plies (takes ages to complete...)
    depth = 64;

  // search position
  main_search_thread =
      launch_search_thread(engine, search_info, hash_table, depth);
  // search_position(engine, search_info, hash_table, depth);
}

// main UCI loop
void uci_loop(engine_t *engine, search_info_t *search_info, tt_t *hash_table) {
  // max hash MB
  int max_hash = 1024;

  // default MB value
  int mb = 128;

// reset STDIN & STDOUT buffers
#ifndef WIN64
  setbuf(stdin, NULL);
#endif
  setbuf(stdout, NULL);

  // define user / GUI input buffer
  char input[10000];

  // print engine info
  printf("Quanticade %s by DarkNeutrino\n", version);

  // main loop
  while (1) {
    // reset user /GUI input
    memset(input, 0, sizeof(input));

    // make sure output reaches the GUI
    fflush(stdout);

    // get user / GUI input
    if (!fgets(input, 10000, stdin))
      // continue the loop
      continue;

    // make sure input is available
    if (input[0] == '\n')
      // continue the loop
      continue;

    // parse UCI "isready" command
    if (strncmp(input, "isready", 7) == 0) {
      printf("readyok\n");
      continue;
    }

    // parse UCI "position" command
    else if (strncmp(input, "position", 8) == 0) {
      // call parse position function
      parse_position(engine, input);
    }
    // parse UCI "ucinewgame" command
    else if (strncmp(input, "ucinewgame", 10) == 0) {
      // call parse position function
      parse_position(engine, "position startpos");

      // clear hash table
      clear_hash_table(hash_table);
    }
    // parse UCI "go" command
    else if (strncmp(input, "go", 2) == 0)
      // call parse go function
      parse_go(engine, search_info, hash_table, input);

    // parse UCI "quit" command
    else if (strncmp(input, "quit", 4) == 0) {
      // quit from the UCI loop (terminate program)
      search_info->stopped = 1;
      pthread_join(main_search_thread, NULL);
      break;
    }

    // parse UCI "stop" command
    else if (strncmp(input, "stop", 4) == 0) {
      // quit from the UCI loop (terminate program)
      search_info->stopped = 1;
      pthread_join(main_search_thread, NULL);
    }

    // parse UCI "uci" command
    else if (strncmp(input, "uci", 3) == 0) {
      // print engine info
      printf("id name Quanticade %s\n", version);
      printf("id author DarkNeutrino\n\n");
      printf("option name Hash type spin default 128 min 4 max %d\n", max_hash);
      printf("option name Use NNUE type check default true\n");
      printf("option name EvalFile type string default %s\n",
             engine->nnue_file);
      printf("option name Clear Hash type button\n");
      printf("uciok\n");
    }

    else if (!strncmp(input, "setoption name Hash value ", 26)) {
      // init MB
      sscanf(input, "%*s %*s %*s %*s %d", &mb);

      // adjust MB if going beyond the allowed bounds
      if (mb < 4)
        mb = 4;
      if (mb > max_hash)
        mb = max_hash;

      // set hash table size in MB
      printf("    Set hash table size to %dMB\n", mb);
      init_hash_table(engine, hash_table, mb);
    }

    else if (!strncmp(input, "setoption name Use NNUE value ", 30)) {
      char *use_nnue;
      uint16_t length = strlen(input);
      use_nnue = calloc(length - 30, 1);
      sscanf(input, "%*s %*s %*s %*s %s", use_nnue);

      if (strncmp(use_nnue, "true", 4) == 0) {
        engine->nnue = 1;
      } else {
        engine->nnue = 0;
      }
    }

    else if (!strncmp(input, "setoption name EvalFile value ", 30)) {
      free(engine->nnue_file);
      uint16_t length = strlen(input);
      engine->nnue_file = calloc(length - 30, 1);
      sscanf(input, "%*s %*s %*s %*s %s", engine->nnue_file);
      nnue_init(engine->nnue_file);
    }

    else if (!strncmp(input, "setoption name Clear Hash", 25)) {
      clear_hash_table(hash_table);
    }
  }
}
