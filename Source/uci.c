#include "enums.h"
#include "macros.h"
#include "quanticade.h"
#include "structs.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

      // wtire hash key into a repetition table
      engine->repetition_table[engine->repetition_index] =
          engine->board.hash_key;

      // make move on the chess board
      make_move(engine, move, all_moves);

      // move current character mointer to the end of current move
      while (*current_char && *current_char != ' ')
        current_char++;

      // go to the next move
      current_char++;
    }
  }
}

static void parse_go(engine_t *engine, char *command) {
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
    engine->inc = atoi(argument + 5);

  // match UCI "winc" command
  if ((argument = strstr(command, "winc")) && engine->board.side == white)
    // parse white time increment
    engine->inc = atoi(argument + 5);

  // match UCI "wtime" command
  if ((argument = strstr(command, "wtime")) && engine->board.side == white)
    // parse white time limit
    engine->time = atoi(argument + 6);

  // match UCI "btime" command
  if ((argument = strstr(command, "btime")) && engine->board.side == black)
    // parse black time limit
    engine->time = atoi(argument + 6);

  // match UCI "movestogo" command
  if ((argument = strstr(command, "movestogo")))
    // parse number of moves to go
    engine->movestogo = atoi(argument + 10);

  // match UCI "movetime" command
  if ((argument = strstr(command, "movetime"))) {
    // parse amount of time allowed to spend to make a move
    engine->time = atoi(argument + 9);
    engine->movestogo = 1;
  }

  // match UCI "depth" command
  if ((argument = strstr(command, "depth")))
    // parse search depth
    depth = atoi(argument + 6);

  // init start time
  engine->starttime = get_time_ms();

  // if time control is available
  if (engine->time != -1) {
    // flag we're playing with time control
    engine->timeset = 1;

    // set up timing
    engine->time /= engine->movestogo;

    // lag compensation
    engine->time -= 50;

    // if time is up
    if (engine->time < 0) {
      // restore negative time to 0
      engine->time = 0;

      // inc lag compensation on 0+inc time controls
      engine->inc -= 50;

      // timing for 0 seconds left and no inc
      if (engine->inc < 0)
        engine->inc = 1;
    }

    // init stoptime
    engine->stoptime = engine->starttime + engine->time + engine->inc;
  }

  // if depth is not available
  if (depth == -1)
    // set depth to 64 plies (takes ages to complete...)
    depth = 64;

  // search position
  search_position(engine, depth);
}

// main UCI loop
void uci_loop(engine_t *engine) {
  // max hash MB
  int max_hash = 1024;

  // default MB value
  int mb = 128;

// reset STDIN & STDOUT buffers
#ifndef WIN64
  setbuf(stdin, NULL);
  setbuf(stdout, NULL);
#endif

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
      clear_hash_table(engine);
    }
    // parse UCI "go" command
    else if (strncmp(input, "go", 2) == 0)
      // call parse go function
      parse_go(engine, input);

    // parse UCI "quit" command
    else if (strncmp(input, "quit", 4) == 0)
      // quit from the UCI loop (terminate program)
      break;

    // parse UCI "uci" command
    else if (strncmp(input, "uci", 3) == 0) {
      // print engine info
      printf("id name Quanticade %s\n", version);
      printf("id author DarkNeutrino\n\n");
      printf("option name Hash type spin default 64 min 4 max %d\n", max_hash);
      printf("uciok\n");
    }

    else if (!strncmp(input, "setoption name Hash value ", 26)) {
      // init MB
      sscanf(input, "%*s %*s %*s %*s %d", &mb);

      // adjust MB if going beyond the aloowed bounds
      if (mb < 4)
        mb = 4;
      if (mb > max_hash)
        mb = max_hash;

      // set hash table size in MB
      printf("    Set hash table size to %dMB\n", mb);
      init_hash_table(engine, mb);
    }
  }
}
