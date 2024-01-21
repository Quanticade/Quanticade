// system headers
#include "evaluate.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef WIN64
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "attacks.h"
#include "enums.h"
#include "macros.h"
#include "nnue/nnue.h"
#include "pvtable.h"
#include "structs.h"
#include "uci.h"

const char *square_to_coordinates[] = {
    "a8", "b8", "c8", "d8", "e8", "f8", "g8", "h8", "a7", "b7", "c7",
    "d7", "e7", "f7", "g7", "h7", "a6", "b6", "c6", "d6", "e6", "f6",
    "g6", "h6", "a5", "b5", "c5", "d5", "e5", "f5", "g5", "h5", "a4",
    "b4", "c4", "d4", "e4", "f4", "g4", "h4", "a3", "b3", "c3", "d3",
    "e3", "f3", "g3", "h3", "a2", "b2", "c2", "d2", "e2", "f2", "g2",
    "h2", "a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1",
};
char ascii_pieces[12] = "PNBRQKpnbrqk";
char *unicode_pieces[12] = {"♙", "♘", "♗", "♖", "♕", "♔",
                            "♟︎", "♞", "♝", "♜", "♛", "♚"};
int char_pieces[] = {
    ['P'] = P, ['N'] = N, ['B'] = B, ['R'] = R, ['Q'] = Q, ['K'] = K,
    ['p'] = p, ['n'] = n, ['b'] = b, ['r'] = r, ['q'] = q, ['k'] = k};
char promoted_pieces[] = {[Q] = 'q', [R] = 'r', [B] = 'b', [N] = 'n',
                          [q] = 'q', [r] = 'r', [b] = 'b', [n] = 'n'};

engine_t engine;

/**********************************\
 ==================================

           Random numbers

 ==================================
\**********************************/

// generate 32-bit pseudo legal numbers
uint32_t get_random_U32_number(engine_t *engine) {
  // get current state
  uint32_t number = engine->random_state;

  // XOR shift algorithm
  number ^= number << 13;
  number ^= number >> 17;
  number ^= number << 5;

  // update random number state
  engine->random_state = number;

  // return random number
  return number;
}

// generate 64-bit pseudo legal numbers
uint64_t get_random_uint64_number(engine_t *engine) {
  // define 4 random numbers
  uint64_t n1, n2, n3, n4;

  // init random numbers slicing 16 bits from MS1B side
  n1 = (uint64_t)(get_random_U32_number(engine)) & 0xFFFF;
  n2 = (uint64_t)(get_random_U32_number(engine)) & 0xFFFF;
  n3 = (uint64_t)(get_random_U32_number(engine)) & 0xFFFF;
  n4 = (uint64_t)(get_random_U32_number(engine)) & 0xFFFF;

  // return random number
  return n1 | (n2 << 16) | (n3 << 32) | (n4 << 48);
}

// generate magic number candidate
uint64_t generate_magic_number(engine_t *engine) {
  return get_random_uint64_number(engine) & get_random_uint64_number(engine) &
         get_random_uint64_number(engine);
}

/**********************************\
 ==================================

            Zobrist keys

 ==================================
\**********************************/

// init random hash keys
static inline void init_random_keys(engine_t *engine) {
  // update pseudo random number state
  engine->random_state = 1804289383;

  // loop over piece codes
  for (int piece = P; piece <= k; piece++) {
    // loop over board squares
    for (int square = 0; square < 64; square++)
      // init random piece keys
      engine->keys.piece_keys[piece][square] = get_random_uint64_number(engine);
  }

  // loop over board squares
  for (int square = 0; square < 64; square++)
    // init random enpassant keys
    engine->keys.enpassant_keys[square] = get_random_uint64_number(engine);

  // loop over castling keys
  for (int index = 0; index < 16; index++)
    // init castling keys
    engine->keys.castle_keys[index] = get_random_uint64_number(engine);

  // init random side key
  engine->keys.side_key = get_random_uint64_number(engine);
}

// generate "almost" unique position ID aka hash key from scratch
uint64_t generate_hash_key(engine_t *engine) {
  // final hash key
  uint64_t final_key = 0ULL;

  // temp piece bitboard copy
  uint64_t bitboard;

  // loop over piece bitboards
  for (int piece = P; piece <= k; piece++) {
    // init piece bitboard copy
    bitboard = engine->board.bitboards[piece];

    // loop over the pieces within a bitboard
    while (bitboard) {
      // init square occupied by the piece
      int square = __builtin_ctzll(bitboard);

      // hash piece
      final_key ^= engine->keys.piece_keys[piece][square];

      // pop LS1B
      pop_bit(bitboard, square);
    }
  }

  // if enpassant square is on board
  if (engine->board.enpassant != no_sq)
    // hash enpassant
    final_key ^= engine->keys.enpassant_keys[engine->board.enpassant];

  // hash castling rights
  final_key ^= engine->keys.castle_keys[engine->board.castle];

  // hash the side only if black is to move
  if (engine->board.side == black)
    final_key ^= engine->keys.side_key;

  // return generated hash key
  return final_key;
}

/**********************************\
 ==================================

           Input & Output

 ==================================
\**********************************/

// reset board variables
static inline void reset_board(engine_t *engine) {
  // reset board position (bitboards)
  memset(engine->board.bitboards, 0ULL, sizeof(engine->board.bitboards));

  // reset occupancies (bitboards)
  memset(engine->board.occupancies, 0ULL, sizeof(engine->board.occupancies));

  // reset game state variables
  engine->board.side = 0;
  engine->board.enpassant = no_sq;
  engine->board.castle = 0;

  // reset repetition index
  engine->repetition_index = 0;

  engine->fifty = 0;

  // reset repetition table
  memset(engine->repetition_table, 0ULL, sizeof(engine->repetition_table));
}

// parse FEN string
void parse_fen(engine_t *engine, char *fen) {
  // prepare for new game
  reset_board(engine);

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
        set_bit(engine->board.bitboards[piece], square);

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
          if (get_bit(engine->board.bitboards[bb_piece], square))
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
  (*fen == 'w') ? (engine->board.side = white) : (engine->board.side = black);

  // go to parsing castling rights
  fen += 2;

  // parse castling rights
  while (*fen != ' ') {
    switch (*fen) {
    case 'K':
      engine->board.castle |= wk;
      break;
    case 'Q':
      engine->board.castle |= wq;
      break;
    case 'k':
      engine->board.castle |= bk;
      break;
    case 'q':
      engine->board.castle |= bq;
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
    engine->board.enpassant = rank * 8 + file;
  }

  // no enpassant square
  else
    engine->board.enpassant = no_sq;

  // go to parsing half move counter (increment pointer to FEN string)
  fen++;

  // parse half move counter to init fifty move counter
  engine->fifty = atoi(fen);

  // loop over white pieces bitboards
  for (int piece = P; piece <= K; piece++)
    // populate white occupancy bitboard
    engine->board.occupancies[white] |= engine->board.bitboards[piece];

  // loop over black pieces bitboards
  for (int piece = p; piece <= k; piece++)
    // populate white occupancy bitboard
    engine->board.occupancies[black] |= engine->board.bitboards[piece];

  // init all occupancies
  engine->board.occupancies[both] |= engine->board.occupancies[white];
  engine->board.occupancies[both] |= engine->board.occupancies[black];

  // init hash key
  engine->board.hash_key = generate_hash_key(engine);
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

// reset time control variables
void reset_time_control(engine_t *engine) {
  // reset timing
  engine->quit = 0;
  engine->movestogo = 30;
  engine->time = -1;
  engine->inc = 0;
  engine->starttime = 0;
  engine->stoptime = 0;
  engine->timeset = 0;
  engine->stopped = 0;
}

/**********************************\
 ==================================

              Init all

 ==================================
\**********************************/

// init all variables
void init_all(engine_t *engine, tt_t *hash_table) {
  // init leaper pieces attacks
  init_leapers_attacks(engine);

  // init slider pieces attacks
  init_sliders_attacks(engine, 1);
  init_sliders_attacks(engine, 0);

  // init random keys for hashing purposes
  init_random_keys(engine);

  // init evaluation masks
  init_evaluation_masks(engine);

  // init hash table with default 128 MB
  init_hash_table(engine, hash_table, 128);

  if (engine->nnue) {
    nnue_init("nn-eba324f53044.nnue");
  }
}

/**********************************\
 ==================================

             Main driver

 ==================================
\**********************************/

int main(void) {
  memset(&engine, 0, sizeof(engine));
  engine.board.enpassant = no_sq;
  engine.movestogo = 30;
  engine.time = -1;
  engine.nnue = 1;
  engine.random_state = 1804289383;
  tt_t hash_table = {NULL, 0, 0};
  engine.nnue_file = calloc(21, 1);
  strcpy(engine.nnue_file, "nn-eba324f53044.nnue");
  // init all
  init_all(&engine, &hash_table);

  // connect to GUI
  uci_loop(&engine, &hash_table);

  // free hash table memory on exit
  free(hash_table.hash_entry);

  return 0;
}
