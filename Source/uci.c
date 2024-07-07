/**********************************\
 ==================================

                UCI
          forked from VICE
         by Richard Allbert

 ==================================
\**********************************/

#include "uci.h"
#include "bitboards.h"
#include "enums.h"
#include "movegen.h"
#include "nnue/nnue.h"
// do NOT move nnue.h above nnue/nnue.h"
#include "nnue.h"
#include "perft.h"
#include "pvtable.h"
#include "pyrrhic/tbprobe.h"
#include "search.h"
#include "structs.h"
#include "utils.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern nnue_t nnue;

#define start_position                                                         \
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 "

const char *square_to_coordinates[] = {
    "a8", "b8", "c8", "d8", "e8", "f8", "g8", "h8", "a7", "b7", "c7",
    "d7", "e7", "f7", "g7", "h7", "a6", "b6", "c6", "d6", "e6", "f6",
    "g6", "h6", "a5", "b5", "c5", "d5", "e5", "f5", "g5", "h5", "a4",
    "b4", "c4", "d4", "e4", "f4", "g4", "h4", "a3", "b3", "c3", "d3",
    "e3", "f3", "g3", "h3", "a2", "b2", "c2", "d2", "e2", "f2", "g2",
    "h2", "a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1",
};
int char_pieces[] = {
    ['P'] = P, ['N'] = N, ['B'] = B, ['R'] = R, ['Q'] = Q, ['K'] = K,
    ['p'] = p, ['n'] = n, ['b'] = b, ['r'] = r, ['q'] = q, ['k'] = k};
char promoted_pieces[] = {[Q] = 'q', [R] = 'r', [B] = 'b', [N] = 'n',
                          [q] = 'q', [r] = 'r', [b] = 'b', [n] = 'n'};

//  parse user/GUI move string input (e.g. "e7e8q")
static inline int parse_move(position_t *pos, thread_t *thread,
                             char *move_string) {
  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(pos, move_list);

  // parse source square
  int source_square = (move_string[0] - 'a') + (8 - (move_string[1] - '0')) * 8;
  thread->starttime = 0;
  thread->stoptime = 0;
  thread->timeset = 0;
  // parse target square
  int target_square = (move_string[2] - 'a') + (8 - (move_string[3] - '0')) * 8;

  // loop over the moves within a move list
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // init move
    int move = move_list->entry[move_count].move;

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

static inline void reset_board(position_t *pos) {
  // reset board position (bitboards)
  memset(pos->bitboards, 0ULL, sizeof(pos->bitboards));

  // reset occupancies (bitboards)
  memset(pos->occupancies, 0ULL, sizeof(pos->occupancies));

  // reset game state variables
  pos->side = 0;
  pos->enpassant = no_sq;
  pos->castle = 0;

  // reset repetition index
  pos->repetition_index = 0;

  pos->fifty = 0;

  // reset repetition table
  memset(pos->repetition_table, 0ULL, sizeof(pos->repetition_table));
}

static inline void parse_fen(position_t *pos, char *fen) {
  // prepare for new game
  reset_board(pos);

  // loop over board ranks
  for (int rank = 0; rank < 8; rank++) {
    // loop over board files
    for (int file = 0; file < 8; file++) {
      // init current square
      int square = rank * 8 + file;

      // match ascii pieces within FEN string
      if ((*fen >= 'a' && *fen <= 'z') || (*fen >= 'A' && *fen <= 'Z')) {
        // init piece type
        int piece = char_pieces[*(uint8_t *)fen];

        // set piece on corresponding bitboard
        set_bit(pos->bitboards[piece], square);

        // increment pointer to FEN string
        fen++;
      }

      // match empty square numbers within FEN string
      if (*fen >= '0' && *fen <= '9') {
        // init offset (convert char 0 to int 0)
        int offset = *fen - '0';

        // define piece variable
        int piece = -1;

        // loop over all piece bitboards
        for (int bb_piece = P; bb_piece <= k; bb_piece++) {
          // if there is a piece on current square
          if (get_bit(pos->bitboards[bb_piece], square))
            // get piece code
            piece = bb_piece;
        }

        // on empty current square
        if (piece == -1)
          // decrement file
          file--;

        // adjust file counter
        file += offset;

        // increment pointer to FEN string
        fen++;
      }

      // match rank separator
      if (*fen == '/')
        // increment pointer to FEN string
        fen++;
    }
  }

  // got to parsing side to move (increment pointer to FEN string)
  fen++;

  // parse side to move
  (*fen == 'w') ? (pos->side = white) : (pos->side = black);

  // go to parsing castling rights
  fen += 2;

  // parse castling rights
  while (*fen != ' ') {
    switch (*fen) {
    case 'K':
      pos->castle |= wk;
      break;
    case 'Q':
      pos->castle |= wq;
      break;
    case 'k':
      pos->castle |= bk;
      break;
    case 'q':
      pos->castle |= bq;
      break;
    case '-':
      break;
    }

    // increment pointer to FEN string
    fen++;
  }

  // got to parsing enpassant square (increment pointer to FEN string)
  fen++;

  // parse enpassant square
  if (*fen != '-') {
    // parse enpassant file & rank
    int file = fen[0] - 'a';
    int rank = 8 - (fen[1] - '0');

    // init enpassant square
    pos->enpassant = rank * 8 + file;
  }

  // no enpassant square
  else
    pos->enpassant = no_sq;

  // go to parsing half move counter (increment pointer to FEN string)
  fen++;

  // parse half move counter to init fifty move counter
  pos->fifty = atoi(fen);

  // loop over white pieces bitboards
  for (int piece = P; piece <= K; piece++)
    // populate white occupancy bitboard
    pos->occupancies[white] |= pos->bitboards[piece];

  // loop over black pieces bitboards
  for (int piece = p; piece <= k; piece++)
    // populate white occupancy bitboard
    pos->occupancies[black] |= pos->bitboards[piece];

  // init all occupancies
  pos->occupancies[both] |= pos->occupancies[white];
  pos->occupancies[both] |= pos->occupancies[black];

  // init hash key
  pos->hash_key = generate_hash_key(pos);
}

// parse UCI "position" command
static inline void parse_position(position_t *pos, thread_t *thread,
                                  char *command) {
  // shift pointer to the right where next token begins
  command += 9;

  // init pointer to the current character in the command string
  char *current_char = command;

  // parse UCI "startpos" command
  if (strncmp(command, "startpos", 8) == 0)
    // init chess board with start position
    parse_fen(pos, start_position);

  // parse UCI "fen" command
  else {
    // make sure "fen" command is available within command string
    current_char = strstr(command, "fen");

    // if no "fen" command is available within command string
    if (current_char == NULL)
      // init chess board with start position
      parse_fen(pos, start_position);

    // found "fen" substring
    else {
      // shift pointer to the right where next token begins
      current_char += 4;

      // init chess board with position from FEN string
      parse_fen(pos, current_char);
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
      int move = parse_move(pos, thread, current_char);

      // if no more moves
      if (move == 0)
        // break out of the loop
        break;

      // increment repetition index
      pos->repetition_index++;

      // write hash key into a repetition table
      pos->repetition_table[pos->repetition_index] = pos->hash_key;

      // make move on the chess board
      make_move(pos, move, all_moves);

      // move current character pointer to the end of current move
      while (*current_char && *current_char != ' ')
        current_char++;

      // go to the next move
      current_char++;
    }
  }
}

static inline void time_control(position_t *pos, thread_t *thread, char *line) {
  // reset time control
  thread->stopped = 0;
  thread->quit = 0;
  thread->starttime = 0;
  thread->stoptime = 0;
  thread->timeset = 0;
  memset(&limits, 0, sizeof(limits_t));

  // Default to 1/30 of the time to spend
  limits.movestogo = 30;

  thread->starttime = get_time_ms();

  // init argument
  char *argument = NULL;

  // infinite search
  if ((argument = strstr(line, "infinite"))) {
  }

  if (pos->side == white) {
    if ((argument = strstr(line, "winc"))) {
      limits.inc = atoi(argument + 5);
    }
    if ((argument = strstr(line, "wtime"))) {
      limits.time = atoi(argument + 6);
    }
  } else {
    if ((argument = strstr(line, "binc"))) {
      limits.inc = atoi(argument + 5);
    }
    if ((argument = strstr(line, "btime"))) {
      limits.time = atoi(argument + 6);
    }
  }
  // match UCI "movestogo" command
  if ((argument = strstr(line, "movestogo")))
    // parse number of moves to go
    limits.movestogo = atoi(argument + 10);

  // match UCI "movetime" command
  if ((argument = strstr(line, "movetime"))) {
    // parse amount of time allowed to spend to make a move
    limits.time = atoi(argument + 9);
    limits.movestogo = 1;
  }
  // match UCI "depth" command
  if ((argument = strstr(line, "depth"))) {
    // parse search depth
    limits.depth = atoi(argument + 6);
  }

  if ((argument = strstr(line, "perft"))) {
    limits.depth = atoi(argument + 6);
    perft_test(pos, thread, limits.depth);
  } else {
    limits.depth = limits.depth == 0 ? max_ply : limits.depth;

    if (limits.time) {
      int64_t time_this_move = (limits.time / limits.movestogo) + limits.inc;
      int64_t max_time = limits.time;
      thread->stoptime = thread->starttime + MIN(max_time, time_this_move) - 50;
      thread->timeset = 1;
    } else {
      thread->timeset = 0;
    }
  }
}

static inline void *parse_go(void *searchthread_info) {
  searchthreadinfo_t *sti = (searchthreadinfo_t *)searchthread_info;
  position_t *pos = sti->pos;
  thread_t *thread = sti->thread;
  char *line = sti->line;

  time_control(pos, thread, line);

  search_position(pos, thread);
  return NULL;
}

// print move (for UCI purposes)
void print_move(int move) {
  if (get_move_promoted(move))
    printf("%s%s%c", square_to_coordinates[get_move_source(move)],
           square_to_coordinates[get_move_target(move)],
           promoted_pieces[get_move_promoted(move)]);
  else
    printf("%s%s", square_to_coordinates[get_move_source(move)],
           square_to_coordinates[get_move_target(move)]);
}

// main UCI loop
void uci_loop(position_t *pos, thread_t *thread) {
  // max hash MB
  int max_hash = 65536;

  // default MB value
  int mb = 128;

  pthread_t search_thread;
  searchthreadinfo_t sti;
  sti.thread = thread;
  sti.pos = pos;

// reset STDIN & STDOUT buffers
#ifndef WIN64
  setbuf(stdin, NULL);
#endif
  setbuf(stdout, NULL);

  // define user / GUI input buffer
  char input[10000];

  // print engine info
  printf("Quanticade %s by DarkNeutrino\n", version);

  // Setup engine with start position as default
  parse_position(pos, thread, start_position);

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
      parse_position(pos, thread, input);
    }
    // parse UCI "ucinewgame" command
    else if (strncmp(input, "ucinewgame", 10) == 0) {
      // clear hash table
      clear_hash_table();
    }
    // parse UCI "go" command
    else if (strncmp(input, "go", 2) == 0) {
      // call parse go function
      strncpy(sti.line, input, 10000);
      pthread_create(&search_thread, NULL, &parse_go, &sti);
    }

    else if (strncmp(input, "stop", 4) == 0) {
      thread->stopped = 1;
      pthread_join(search_thread, NULL);
      break;
    }
    // parse UCI "quit" command
    else if (strncmp(input, "quit", 4) == 0)
      // quit from the UCI loop (terminate program)
      break;

    // parse UCI "uci" command
    else if (strncmp(input, "uci", 3) == 0) {
      // print engine info
      printf("id name Quanticade %s\n", version);
      printf("id author DarkNeutrino\n\n");
      printf("option name Hash type spin default 128 min 4 max %d\n", max_hash);
      printf("option name Use NNUE type check default true\n");
      printf("option name EvalFile type string default %s\n", nnue.nnue_file);
      printf("option name Clear Hash type button\n");
      printf("option name SyzygyPath type string default <empty>\n");
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
      init_hash_table(mb);
    }

    else if (!strncmp(input, "setoption name Use NNUE value ", 30)) {
      char *use_nnue;
      uint16_t length = strlen(input);
      use_nnue = calloc(length - 30, 1);
      sscanf(input, "%*s %*s %*s %*s %s", use_nnue);

      if (strncmp(use_nnue, "true", 4) == 0) {
        nnue.use_nnue = 1;
      } else {
        nnue.use_nnue = 0;
      }
    }

    else if (!strncmp(input, "setoption name EvalFile value ", 30)) {
      free(nnue.nnue_file);
      uint16_t length = strlen(input);
      nnue.nnue_file = calloc(length - 30, 1);
      sscanf(input, "%*s %*s %*s %*s %s", nnue.nnue_file);
      nnue_init(nnue.nnue_file);
    }

    else if (!strncmp(input, "setoption name Clear Hash", 25)) {
      clear_hash_table();
    } else if (!strncmp(input, "setoption name SyzygyPath value ", 32)) {
      char *ptr = input + 32;
      tb_init(ptr);
      printf("info string set SyzygyPath to %s\n", ptr);
    }
  }
}
