// system headers
#include <math.h>
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
#include "evaluate.h"
#include "macros.h"
#include "movegen.h"
#include "nnue/nnue.h"
#include "structs.h"
#include "uci.h"
#include "utils.h"

const int full_depth_moves = 4;
const int reduction_limit = 3;
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
const int mvv_lva[12][12] = {
    {105, 205, 305, 405, 505, 605, 105, 205, 305, 405, 505, 605},
    {104, 204, 304, 404, 504, 604, 104, 204, 304, 404, 504, 604},
    {103, 203, 303, 403, 503, 603, 103, 203, 303, 403, 503, 603},
    {102, 202, 302, 402, 502, 602, 102, 202, 302, 402, 502, 602},
    {101, 201, 301, 401, 501, 601, 101, 201, 301, 401, 501, 601},
    {100, 200, 300, 400, 500, 600, 100, 200, 300, 400, 500, 600},

    {105, 205, 305, 405, 505, 605, 105, 205, 305, 405, 505, 605},
    {104, 204, 304, 404, 504, 604, 104, 204, 304, 404, 504, 604},
    {103, 203, 303, 403, 503, 603, 103, 203, 303, 403, 503, 603},
    {102, 202, 302, 402, 502, 602, 102, 202, 302, 402, 502, 602},
    {101, 201, 301, 401, 501, 601, 101, 201, 301, 401, 501, 601},
    {100, 200, 300, 400, 500, 600, 100, 200, 300, 400, 500, 600}};

engine_t engine;

// a bridge function to interact between search and GUI input
void communicate(engine_t *engine) {
  // if time is up break here
  if (engine->timeset == 1 && get_time_ms() > engine->stoptime) {
    // tell engine to stop calculating
    engine->stopped = 1;
  }

  // read GUI input
  read_input(engine);
}

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
void init_random_keys(engine_t *engine) {
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
void reset_board(engine_t *engine) {
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

// print move list
void print_move_list(moves *move_list) {
  // do nothing on empty move list
  if (!move_list->count) {
    printf("\n     No move in the move list!\n");
    return;
  }

  printf("\n     move    piece     capture   double    enpass    castling\n\n");

  // loop over moves within a move list
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // init move
    int move = move_list->entry[move_count].move;

#ifdef WIN64
    // print move
    printf("      %s%s%c   %c         %d         %d         %d         %d\n",
           square_to_coordinates[get_move_source(move)],
           square_to_coordinates[get_move_target(move)],
           get_move_promoted(move) ? promoted_pieces[get_move_promoted(move)]
                                   : ' ',
           ascii_pieces[get_move_piece(move)], get_move_capture(move) ? 1 : 0,
           get_move_double(move) ? 1 : 0, get_move_enpassant(move) ? 1 : 0,
           get_move_castling(move) ? 1 : 0);
#else
    // print move
    printf("     %s%s%c   %s         %d         %d         %d         %d\n",
           square_to_coordinates[get_move_source(move)],
           square_to_coordinates[get_move_target(move)],
           get_move_promoted(move) ? promoted_pieces[get_move_promoted(move)]
                                   : ' ',
           unicode_pieces[get_move_piece(move)], get_move_capture(move) ? 1 : 0,
           get_move_double(move) ? 1 : 0, get_move_enpassant(move) ? 1 : 0,
           get_move_castling(move) ? 1 : 0);
#endif
  }

  // print total number of moves
  printf("\n\n     Total number of moves: %d\n\n", move_list->count);
}

// make move on chess board

/**********************************\
 ==================================

               Perft

 ==================================
\**********************************/

// perft driver
static inline void perft_driver(engine_t *engine, int depth) {
  // recursion escape condition
  if (depth == 0) {
    // increment nodes count (count reached positions)
    engine->nodes++;
    return;
  }

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(engine, move_list);

  // loop over generated moves
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    // make move
    if (!make_move(engine, move_list->entry[move_count].move, all_moves))
      // skip to the next move
      continue;

    // call perft driver recursively
    perft_driver(engine, depth - 1);

    // take back
    restore_board(engine->board.bitboards, engine->board.occupancies,
                  engine->board.side, engine->board.enpassant,
                  engine->board.castle, engine->fifty, engine->board.hash_key);
  }
}

// perft test
void perft_test(engine_t *engine, int depth) {
  printf("\n     Performance test\n\n");

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(engine, move_list);

  // init start time
  long start = get_time_ms();

  // loop over generated moves
  for (uint32_t move_count = 0; move_count < move_list->count; move_count++) {
    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    // make move
    if (!make_move(engine, move_list->entry[move_count].move, all_moves))
      // skip to the next move
      continue;

    // cummulative nodes
    long cummulative_nodes = engine->nodes;

    // call perft driver recursively
    perft_driver(engine, depth - 1);

    // old nodes
    long old_nodes = engine->nodes - cummulative_nodes;

    // take back
    restore_board(engine->board.bitboards, engine->board.occupancies,
                  engine->board.side, engine->board.enpassant,
                  engine->board.castle, engine->fifty, engine->board.hash_key);

    // print move
    printf("     move: %s%s%c  nodes: %ld\n",
           square_to_coordinates[get_move_source(
               move_list->entry[move_count].move)],
           square_to_coordinates[get_move_target(
               move_list->entry[move_count].move)],
           get_move_promoted(move_list->entry[move_count].move)
               ? promoted_pieces[get_move_promoted(
                     move_list->entry[move_count].move)]
               : ' ',
           old_nodes);
  }

  // print results
  printf("\n    Depth: %d\n", depth);
#ifdef WIN64
  printf("    Nodes: %llu\n", engine->nodes);
  printf("     Time: %llu\n\n", get_time_ms() - start);
#else
  printf("    Nodes: %lu\n", engine->nodes);
  printf("     Time: %lu\n\n", get_time_ms() - start);
#endif
}

/**********************************\
 ==================================

             Evaluation

 ==================================
\**********************************/

// set file or rank mask
uint64_t set_file_rank_mask(int file_number, int rank_number) {
  if (file_number >= 0) {
    // File mask
    return 0x0101010101010101ULL << file_number;
  } else if (rank_number >= 0) {
    // Rank mask
    return 0xFFULL << (8 * rank_number);
  } else {
    // Invalid input
    return 0ULL;
  }
}

// init evaluation masks
void init_evaluation_masks(engine_t *engine) {
  /******** Init file masks ********/

  // loop over ranks
  for (int rank = 0; rank < 8; rank++) {
    // loop over files
    for (int file = 0; file < 8; file++) {
      // init square
      int square = rank * 8 + file;

      // init file mask for a current square
      engine->masks.file_masks[square] |= set_file_rank_mask(file, -1);

      // init rank mask for a current square
      engine->masks.rank_masks[square] |= set_file_rank_mask(-1, rank);

      // init isolated pawns masks for a current square
      engine->masks.isolated_masks[square] |= set_file_rank_mask(file - 1, -1);
      engine->masks.isolated_masks[square] |= set_file_rank_mask(file + 1, -1);

      // init white passed pawns mask for a current square
      engine->masks.white_passed_masks[square] |=
          set_file_rank_mask(file - 1, -1);
      engine->masks.white_passed_masks[square] |= set_file_rank_mask(file, -1);
      engine->masks.white_passed_masks[square] |=
          set_file_rank_mask(file + 1, -1);

      // init black passed pawns mask for a current square
      engine->masks.black_passed_masks[square] |=
          set_file_rank_mask(file - 1, -1);
      engine->masks.black_passed_masks[square] |= set_file_rank_mask(file, -1);
      engine->masks.black_passed_masks[square] |=
          set_file_rank_mask(file + 1, -1);

      // loop over redundant ranks
      for (int i = 0; i < (8 - rank); i++) {
        // reset redundant bits
        engine->masks.white_passed_masks[square] &=
            ~engine->masks.rank_masks[(7 - i) * 8 + file];
      }

      // loop over redundant ranks
      for (int i = 0; i < rank + 1; i++) {
        // reset redundant bits
        engine->masks.black_passed_masks[square] &=
            ~engine->masks.rank_masks[i * 8 + file];
      }
    }
  }
}

/**********************************\
 ==================================

               Search

 ==================================
\**********************************/

// clear TT (hash table)
void clear_hash_table(tt_t *hash_table) {
  memset(hash_table->hash_entry, 0,
         sizeof(tt_entry_t) * hash_table->num_of_entries);
  hash_table->current_age = 0;
}

// dynamically allocate memory for hash table
void init_hash_table(engine_t *engine, tt_t *hash_table, int mb) {
  // init hash size
  int hash_size = 0x100000 * mb;

  // init number of hash entries
  hash_table->num_of_entries = hash_size / sizeof(tt_entry_t);

  // free hash table if not empty
  if (hash_table->hash_entry != NULL) {
    printf("    Clearing hash memory...\n");

    // free hash table dynamic memory
    free(hash_table->hash_entry);
  }

  // allocate memory
  hash_table->hash_entry =
      (tt_entry_t *)malloc(hash_table->num_of_entries * sizeof(tt_entry_t));

  // if allocation has failed
  if (hash_table->hash_entry == NULL) {
    printf("    Couldn't allocate memory for hash table, trying %dMB...",
           mb / 2);

    // try to allocate with half size
    init_hash_table(engine, hash_table, mb / 2);
  }

  // if allocation succeeded
  else {
    // clear hash table
    clear_hash_table(hash_table);
  }
}

// read hash entry data
static inline int read_hash_entry(engine_t *engine, tt_t *hash_table, int alpha,
                                  int *move, int beta, int depth) {
  // create a TT instance pointer to particular hash entry storing
  // the scoring data for the current board position if available
  tt_entry_t *hash_entry =
      &hash_table
           ->hash_entry[engine->board.hash_key % hash_table->num_of_entries];

  // make sure we're dealing with the exact position we need
  if (hash_entry->hash_key == engine->board.hash_key) {
    // make sure that we match the exact depth our search is now at
    if (hash_entry->depth >= depth) {
      // extract stored score from TT entry
      int score = hash_entry->score;

      // retrieve score independent from the actual path
      // from root node (position) to current node (position)
      if (score < -mate_score)
        score += engine->ply;
      if (score > mate_score)
        score -= engine->ply;

      // match the exact (PV node) score
      if (hash_entry->flag == hash_flag_exact)
        // return exact (PV node) score
        return score;

      // match alpha (fail-low node) score
      if ((hash_entry->flag == hash_flag_alpha) && (score <= alpha))
        // return alpha (fail-low node) score
        return alpha;

      // match beta (fail-high node) score
      if ((hash_entry->flag == hash_flag_beta) && (score >= beta))
        // return beta (fail-high node) score
        return beta;
    }
    *move = hash_entry->move;
  }

  // if hash entry doesn't exist
  return no_hash_entry;
}

// write hash entry data
static inline void write_hash_entry(engine_t *engine, tt_t *hash_table,
                                    int score, int depth, int move,
                                    int hash_flag) {
  // create a TT instance pointer to particular hash entry storing
  // the scoring data for the current board position if available
  tt_entry_t *hash_entry =
      &hash_table
           ->hash_entry[engine->board.hash_key % hash_table->num_of_entries];

  if (!(hash_entry->hash_key == 0 ||
        (hash_entry->age < hash_table->current_age ||
         hash_entry->depth <= depth))) {
    return;
  }

  // store score independent from the actual path
  // from root node (position) to current node (position)
  if (score < -mate_score)
    score -= engine->ply;
  if (score > mate_score)
    score += engine->ply;

  // write hash entry data
  hash_entry->hash_key = engine->board.hash_key;
  hash_entry->score = score;
  hash_entry->flag = hash_flag;
  hash_entry->depth = depth;
  hash_entry->move = move;
  hash_entry->age = hash_table->current_age;
}

// enable PV move scoring
static inline void enable_pv_scoring(engine_t *engine, moves *move_list) {
  // disable following PV
  engine->follow_pv = 0;

  // loop over the moves within a move list
  for (uint32_t count = 0; count < move_list->count; count++) {
    // make sure we hit PV move
    if (engine->pv_table[0][engine->ply] == move_list->entry[count].move) {
      // enable move scoring
      engine->score_pv = 1;

      // enable following PV
      engine->follow_pv = 1;
    }
  }
}

/*  =======================
         Move ordering
    =======================

    1. PV move
    2. Captures in MVV/LVA
    3. 1st killer move
    4. 2nd killer move
    5. History moves
    6. Unsorted moves
*/

// score moves
static inline void score_move(engine_t *engine, move_t *move_entry,
                              int hash_move) {
  int move = move_entry->move;
  if (move == hash_move) {
    move_entry->score = 30000;
    return;
  }

  // if PV move scoring is allowed
  if (engine->score_pv) {
    // make sure we are dealing with PV move
    if (engine->pv_table[0][engine->ply] == move) {
      // disable score PV flag
      engine->score_pv = 0;

      // give PV move the highest score to search it first
      move_entry->score = 20000;
      return;
    }
  }

  // score capture move
  if (get_move_capture(move)) {
    // init target piece
    int target_piece = P;

    // pick up bitboard piece index ranges depending on side
    int start_piece, end_piece;

    // pick up side to move
    if (engine->board.side == white) {
      start_piece = p;
      end_piece = k;
    } else {
      start_piece = P;
      end_piece = K;
    }

    // loop over bitboards opposite to the current side to move
    for (int bb_piece = start_piece; bb_piece <= end_piece; bb_piece++) {
      // if there's a piece on the target square
      if (get_bit(engine->board.bitboards[bb_piece], get_move_target(move))) {
        // remove it from corresponding bitboard
        target_piece = bb_piece;
        break;
      }
    }

    // score move by MVV LVA lookup [source piece][target piece]
    move_entry->score = mvv_lva[get_move_piece(move)][target_piece] + 10000;
    return;
  }

  // score quiet move
  else {
    // score 1st killer move
    if (engine->killer_moves[0][engine->ply] == move) {
      move_entry->score = 9000;
    }

    // score 2nd killer move
    else if (engine->killer_moves[1][engine->ply] == move) {
      move_entry->score = 8000;
    }

    // score history move
    else {
      move_entry->score =
          engine->history_moves[get_move_piece(move)][get_move_target(move)];
    }

    return;
  }

  move_entry->score = 0;
  return;
}

// sort moves in descending order
static inline void sort_moves(moves *move_list) {
  // loop over current move within a move list
  for (uint32_t current_move = 0; current_move < move_list->count;
       current_move++) {
    // loop over next move within a move list
    for (uint32_t next_move = current_move + 1; next_move < move_list->count;
         next_move++) {
      // compare current and next move scores
      if (move_list->entry[current_move].score <
          move_list->entry[next_move].score) {
        // swap scores
        int temp_score = move_list->entry[current_move].score;
        move_list->entry[current_move].score =
            move_list->entry[next_move].score;
        move_list->entry[next_move].score = temp_score;

        // swap moves
        int temp_move = move_list->entry[current_move].move;
        move_list->entry[current_move].move = move_list->entry[next_move].move;
        move_list->entry[next_move].move = temp_move;
      }
    }
  }
}

// print move scores
void print_move_scores(engine_t *engine, moves *move_list) {
  (void)engine;
  printf("     Move scores:\n\n");

  // loop over moves within a move list
  for (uint32_t count = 0; count < move_list->count; count++) {
    printf("     move: ");
    print_move(move_list->entry[count].move);
    printf(" score: %d\n", move_list->entry[count].score);
  }
}

// position repetition detection
static inline int is_repetition(engine_t *engine) {
  // loop over repetition indices range
  for (uint32_t index = 0; index < engine->repetition_index; index++)
    // if we found the hash key same with a current
    if (engine->repetition_table[index] == engine->board.hash_key)
      // we found a repetition
      return 1;

  // if no repetition found
  return 0;
}

// quiescence search
static inline int quiescence(engine_t *engine, int alpha, int beta) {
  // every 2047 nodes
  if ((engine->nodes & 2047) == 0)
    // "listen" to the GUI/user input
    communicate(engine);

  // we are too deep, hence there's an overflow of arrays relying on max ply
  // constant
  if (engine->ply > max_ply - 1)
    // evaluate position
    return evaluate(engine);

  // evaluate position
  int evaluation = evaluate(engine);

  // fail-hard beta cutoff
  if (evaluation >= beta) {
    // node (position) fails high
    return beta;
  }

  // found a better move
  if (evaluation > alpha) {
    // PV node (position)
    alpha = evaluation;
  }

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_captures(engine, move_list);

  for (uint32_t count = 0; count < move_list->count; count++) {
    score_move(engine, &move_list->entry[count], 0);
  }

  sort_moves(move_list);

  // loop over moves within a movelist
  for (uint32_t count = 0; count < move_list->count; count++) {
    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    // increment ply
    engine->ply++;

    // increment repetition index & store hash key
    engine->repetition_index++;
    engine->repetition_table[engine->repetition_index] = engine->board.hash_key;

    // make sure to make only legal moves
    if (make_move(engine, move_list->entry[count].move, only_captures) == 0) {
      // decrement ply
      engine->ply--;

      // decrement repetition index
      engine->repetition_index--;

      // skip to next move
      continue;
    }

    // score current move
    int score = -quiescence(engine, -beta, -alpha);

    // decrement ply
    engine->ply--;

    // decrement repetition index
    engine->repetition_index--;

    // take move back
    restore_board(engine->board.bitboards, engine->board.occupancies,
                  engine->board.side, engine->board.enpassant,
                  engine->board.castle, engine->fifty, engine->board.hash_key);

    // reutrn 0 if time is up
    if (engine->stopped == 1) {
      return 0;
    }

    // found a better move
    if (score > alpha) {
      // PV node (position)
      alpha = score;

      // fail-hard beta cutoff
      if (score >= beta) {
        // node (position) fails high
        return beta;
      }
    }
  }

  // node (position) fails low
  return alpha;
}

// negamax alpha beta search
static inline int negamax(engine_t *engine, tt_t *hash_table, int alpha,
                          int beta, int depth) {
  // init PV length
  engine->pv_length[engine->ply] = engine->ply;

  // variable to store current move's score (from the static evaluation
  // perspective)
  int score;

  int move = 0;

  // define hash flag
  int hash_flag = hash_flag_alpha;

  // if position repetition occurs
  if ((is_repetition(engine) || engine->fifty >= 100) && engine->ply)
    // return draw score
    return 0;

  // a hack by Pedro Castro to figure out whether the current node is PV node or
  // not
  int pv_node = beta - alpha > 1;

  // read hash entry if we're not in a root ply and hash entry is available
  // and current node is not a PV node
  if (engine->ply &&
      (score = read_hash_entry(engine, hash_table, alpha, &move, beta,
                               depth)) != no_hash_entry &&
      pv_node == 0)
    // if the move has already been searched (hence has a value)
    // we just return the score for this move without searching it
    return score;

  // every 2047 nodes
  if ((engine->nodes & 2047) == 0)
    // "listen" to the GUI/user input
    communicate(engine);

  // recursion escapre condition
  if (depth == 0) {
    // run quiescence search
    return quiescence(engine, alpha, beta);
  }

  // we are too deep, hence there's an overflow of arrays relying on max ply
  // constant
  if (engine->ply > max_ply - 1)
    // evaluate position
    return evaluate(engine);

  // increment nodes count
  engine->nodes++;

  // is king in check
  int in_check =
      is_square_attacked(engine,
                         (engine->board.side == white)
                             ? __builtin_ctzll(engine->board.bitboards[K])
                             : __builtin_ctzll(engine->board.bitboards[k]),
                         engine->board.side ^ 1);

  // increase search depth if the king has been exposed into a check
  if (in_check)
    depth++;

  // legal moves counter
  int legal_moves = 0;

  int static_eval = evaluate(engine);

  // evaluation pruning / static null move pruning
  if (depth < 3 && !pv_node && !in_check && abs(beta - 1) > -infinity + 100) {
    // get static evaluation score

    // define evaluation margin
    int eval_margin = 120 * depth;

    // evaluation margin substracted from static evaluation score fails high
    if (static_eval - eval_margin >= beta)
      // evaluation margin substracted from static evaluation score
      return static_eval - eval_margin;
  }

  // null move pruning
  if (depth >= 3 && in_check == 0 && engine->ply) {
    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    // increment ply
    engine->ply++;

    // increment repetition index & store hash key
    engine->repetition_index++;
    engine->repetition_table[engine->repetition_index] = engine->board.hash_key;

    // hash enpassant if available
    if (engine->board.enpassant != no_sq)
      engine->board.hash_key ^=
          engine->keys.enpassant_keys[engine->board.enpassant];

    // reset enpassant capture square
    engine->board.enpassant = no_sq;

    // switch the side, literally giving opponent an extra move to make
    engine->board.side ^= 1;

    // hash the side
    engine->board.hash_key ^= engine->keys.side_key;

    /* search moves with reduced depth to find beta cutoffs
       depth - 1 - R where R is a reduction limit */
    score = -negamax(engine, hash_table, -beta, -beta + 1, depth - 1 - 2);

    // decrement ply
    engine->ply--;

    // decrement repetition index
    engine->repetition_index--;

    // restore board state
    restore_board(engine->board.bitboards, engine->board.occupancies,
                  engine->board.side, engine->board.enpassant,
                  engine->board.castle, engine->fifty, engine->board.hash_key);

    // reutrn 0 if time is up
    if (engine->stopped == 1) {
      return 0;
    }

    // fail-hard beta cutoff
    if (score >= beta)
      // node (position) fails high
      return beta;
  }

  // razoring
  if (!pv_node && !in_check && depth <= 3) {
    // get static eval and add first bonus
    score = static_eval + 125;

    // define new score
    int new_score;

    // static evaluation indicates a fail-low node
    if (score < beta) {
      // on depth 1
      if (depth == 1) {
        // get quiescence score
        new_score = quiescence(engine, alpha, beta);

        // return quiescence score if it's greater then static evaluation score
        return (new_score > score) ? new_score : score;
      }

      // add second bonus to static evaluation
      score += 175;

      // static evaluation indicates a fail-low node
      if (score < beta && depth <= 2) {
        // get quiescence score
        new_score = quiescence(engine, alpha, beta);

        // quiescence score indicates fail-low node
        if (new_score < beta)
          // return quiescence score if it's greater then static evaluation
          // score
          return (new_score > score) ? new_score : score;
      }
    }
  }

  // create move list instance
  moves move_list[1];

  // generate moves
  generate_moves(engine, move_list);

  // if we are now following PV line
  if (engine->follow_pv)
    // enable PV move scoring
    enable_pv_scoring(engine, move_list);

  for (uint32_t count = 0; count < move_list->count; count++) {
    score_move(engine, &move_list->entry[count], move);
  }

  sort_moves(move_list);

  // number of moves searched in a move list
  int moves_searched = 0;

  // loop over moves within a movelist
  for (uint32_t count = 0; count < move_list->count; count++) {

    // preserve board state
    copy_board(engine->board.bitboards, engine->board.occupancies,
               engine->board.side, engine->board.enpassant,
               engine->board.castle, engine->fifty, engine->board.hash_key);

    int list_move = move_list->entry[count].move;

    // increment ply
    engine->ply++;

    // increment repetition index & store hash key
    engine->repetition_index++;
    engine->repetition_table[engine->repetition_index] = engine->board.hash_key;

    // make sure to make only legal moves
    if (make_move(engine, list_move, all_moves) == 0) {
      // decrement ply
      engine->ply--;

      // decrement repetition index
      engine->repetition_index--;

      // skip to next move
      continue;
    }

    // increment legal moves
    legal_moves++;

    // full depth search
    if (moves_searched == 0) {
      // do normal alpha beta search
      score = -negamax(engine, hash_table, -beta, -alpha, depth - 1);
    }

    // late move reduction (LMR)
    else {
      // condition to consider LMR
      if (moves_searched >= full_depth_moves && depth >= reduction_limit &&
          in_check == 0 && get_move_capture(list_move) == 0 &&
          get_move_promoted(list_move) == 0) {
        // search current move with reduced depth:
        score = -negamax(engine, hash_table, -alpha - 1, -alpha, depth - 2);
      }

      // hack to ensure that full-depth search is done
      else {
        score = alpha + 1;
      }

      // principle variation search PVS
      if (score > alpha) {
        /* Once you've found a move with a score that is between alpha and beta,
           the rest of the moves are searched with the goal of proving that they
           are all bad. It's possible to do this a bit faster than a search that
           worries that one of the remaining moves might be good. */
        engine->nodes++;
        score = -negamax(engine, hash_table, -alpha - 1, -alpha, depth - 1);

        /* If the algorithm finds out that it was wrong, and that one of the
           subsequent moves was better than the first PV move, it has to search
           again, in the normal alpha-beta manner.  This happens sometimes, and
           it's a waste of time, but generally not often enough to counteract
           the savings gained from doing the "bad move proof" search referred to
           earlier. */
        if ((score > alpha) && (score < beta)) {
          /* re-search the move that has failed to be proved to be bad
             with normal alpha beta score bounds*/
          score = -negamax(engine, hash_table, -beta, -alpha, depth - 1);
        }
      }
    }

    // decrement ply
    engine->ply--;

    // decrement repetition index
    engine->repetition_index--;

    // take move back
    restore_board(engine->board.bitboards, engine->board.occupancies,
                  engine->board.side, engine->board.enpassant,
                  engine->board.castle, engine->fifty, engine->board.hash_key);

    // return infinity so we can deal with timeout in case we are doing
    // re-search
    if (engine->stopped == 1) {
      return infinity;
    }

    // increment the counter of moves searched so far
    moves_searched++;

    // found a better move
    if (score > alpha) {
      // switch hash flag from storing score for fail-low node
      // to the one storing score for PV node
      hash_flag = hash_flag_exact;

      move = list_move;

      // on quiet moves
      if (get_move_capture(list_move) == 0)
        // store history moves
        engine->history_moves[get_move_piece(list_move)]
                             [get_move_target(list_move)] += depth;

      // PV node (position)
      alpha = score;

      // write PV move
      engine->pv_table[engine->ply][engine->ply] = list_move;

      // loop over the next ply
      for (int next_ply = engine->ply + 1;
           next_ply < engine->pv_length[engine->ply + 1]; next_ply++)
        // copy move from deeper ply into a current ply's line
        engine->pv_table[engine->ply][next_ply] =
            engine->pv_table[engine->ply + 1][next_ply];

      // adjust PV length
      engine->pv_length[engine->ply] = engine->pv_length[engine->ply + 1];

      // fail-hard beta cutoff
      if (score >= beta) {
        // store hash entry with the score equal to beta
        write_hash_entry(engine, hash_table, beta, depth, move, hash_flag_beta);

        // on quiet moves
        if (get_move_capture(list_move) == 0) {
          // store killer moves
          engine->killer_moves[1][engine->ply] =
              engine->killer_moves[0][engine->ply];
          engine->killer_moves[0][engine->ply] = list_move;
        }

        // node (position) fails high
        return beta;
      }
    }
  }

  // we don't have any legal moves to make in the current postion
  if (legal_moves == 0) {
    // king is in check
    if (in_check)
      // return mating score (assuming closest distance to mating position)
      return -mate_value + engine->ply;

    // king is not in check
    else
      // return stalemate score
      return 0;
  }

  // store hash entry with the score equal to alpha
  write_hash_entry(engine, hash_table, alpha, depth, move, hash_flag);

  // node (position) fails low
  return alpha;
}

// search position for the best move
void search_position(engine_t *engine, tt_t *hash_table, int depth) {
  // search start time
  uint64_t start = get_time_ms();

  // define best score variable
  int score = 0;

  int pv_table_copy[max_ply][max_ply];
  int pv_length_copy[max_ply];

  uint8_t window_ok = 1;

  // reset nodes counter
  engine->nodes = 0;

  // reset "time is up" flag
  engine->stopped = 0;

  // reset follow PV flags
  engine->follow_pv = 0;
  engine->score_pv = 0;

  hash_table->current_age++;

  // clear helper data structures for search
  memset(engine->killer_moves, 0, sizeof(engine->killer_moves));
  memset(engine->history_moves, 0, sizeof(engine->history_moves));
  memset(engine->pv_table, 0, sizeof(engine->pv_table));
  memset(engine->pv_length, 0, sizeof(engine->pv_length));

  // define initial alpha beta bounds
  int alpha = -infinity;
  int beta = infinity;

  // iterative deepening
  for (int current_depth = 1; current_depth <= depth; current_depth++) {
    // if time is up
    if (engine->stopped == 1) {
      // stop calculating and return best move so far
      break;
    }

    // enable follow PV flag
    engine->follow_pv = 1;

    // We should not save PV move from unfinished depth for example if depth 12
    // finishes and goes to search depth 13 now but this triggers window cutoff
    // we dont want the info from depth 13 as its incomplete and in case depth
    // 14 search doesnt finish in time we will at least have an full PV line
    // from depth 12
    if (window_ok) {
      memcpy(pv_table_copy, engine->pv_table, sizeof(engine->pv_table));
      memcpy(pv_length_copy, engine->pv_length, sizeof(engine->pv_length));
    }

    // find best move within a given position
    score = negamax(engine, hash_table, alpha, beta, current_depth);

    // Reset aspiration window OK flag back to 1
    window_ok = 1;

    // We hit an apspiration window cut-off before time ran out and we jumped to
    // another depth with wider search which we didnt finish
    if (score == infinity) {
      // Restore the saved best line
      memcpy(engine->pv_table, pv_table_copy, sizeof(pv_table_copy));
      memcpy(engine->pv_length, pv_length_copy, sizeof(pv_length_copy));
      // Break out of the loop without printing info about the unfinished depth
      break;
    }

    // we fell outside the window, so try again with a full-width window (and
    // the same depth)
    if ((score <= alpha) || (score >= beta)) {
      // Do a full window re-search
      alpha = -infinity;
      beta = infinity;
      window_ok = 0;
      current_depth--;
      continue;
    }

    // set up the window for the next iteration
    alpha = score - 50;
    beta = score + 50;

    // if PV is available
    if (engine->pv_length[0]) {
      // print search info
      uint64_t time = get_time_ms() - start;
      uint64_t nps = (engine->nodes / fmax(time, 1)) * 100;
      if (score > -mate_value && score < -mate_score) {
#ifdef WIN64
        printf("info depth %d score mate %d nodes %llu nps %llu time %llu pv ",
               current_depth, -(score + mate_value) / 2 - 1, engine->nodes, nps,
               time);
#else
        printf("info depth %d score mate %d nodes %lu nps %ld time %lu pv ",
               current_depth, -(score + mate_value) / 2 - 1, engine->nodes, nps,
               time);
#endif
      }

      else if (score > mate_score && score < mate_value) {
#ifdef WIN64
        printf("info depth %d score mate %d nodes %llu nps %llu time %llu pv ",
               current_depth, (mate_value - score) / 2 + 1, engine->nodes, nps,
               time);
#else
        printf("info depth %d score mate %d nodes %lu nps %ld time %lu pv ",
               current_depth, (mate_value - score) / 2 + 1, engine->nodes, nps,
               time);
#endif
      }

      else {
#ifdef WIN64
        printf("info depth %d score cp %d nodes %llu nps %llu time %llu pv ",
               current_depth, score, engine->nodes, nps, time);
#else
        printf("info depth %d score cp %d nodes %lu nps %ld time %lu pv ",
               current_depth, score, engine->nodes, nps, time);
#endif
      }

      // loop over the moves within a PV line
      for (int count = 0; count < engine->pv_length[0]; count++) {
        // print PV move
        print_move(engine->pv_table[0][count]);
        printf(" ");
      }

      // print new line
      printf("\n");
    }
  }

  // print best move
  printf("bestmove ");
  if (engine->pv_table[0][0]) {
    print_move(engine->pv_table[0][0]);
  } else {
    printf("(none)");
  }
  printf("\n");
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
