#include "pvtable.h"
#include "bitboards.h"
#include "enums.h"
#include "structs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

tt_t tt;

uint64_t generate_hash_key(engine_t *engine, position_t *pos) {
  // final hash key
  uint64_t final_key = 0ULL;

  // temp piece bitboard copy
  uint64_t bitboard;

  // loop over piece bitboards
  for (int piece = P; piece <= k; piece++) {
    // init piece bitboard copy
    bitboard = pos->bitboards[piece];

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
  if (pos->enpassant != no_sq)
    // hash enpassant
    final_key ^= engine->keys.enpassant_keys[pos->enpassant];

  // hash castling rights
  final_key ^= engine->keys.castle_keys[pos->castle];

  // hash the side only if black is to move
  if (pos->side == black)
    final_key ^= engine->keys.side_key;

  // return generated hash key
  return final_key;
}

void clear_hash_table() {
  memset(tt.hash_entry, 0,
         sizeof(tt_entry_t) * tt.num_of_entries);
  tt.current_age = 0;
}

// dynamically allocate memory for hash table
void init_hash_table(engine_t *engine, uint64_t mb) {
  // init hash size
  uint64_t hash_size = 0x100000LL * mb;

  // init number of hash entries
  tt.num_of_entries = hash_size / sizeof(tt_entry_t);

  // free hash table if not empty
  if (tt.hash_entry != NULL) {
    printf("    Clearing hash memory...\n");

    // free hash table dynamic memory
    free(tt.hash_entry);
  }

  // allocate memory
  tt.hash_entry =
      (tt_entry_t *)malloc(tt.num_of_entries * sizeof(tt_entry_t));

  // if allocation has failed
  if (tt.hash_entry == NULL) {
    printf("    Couldn't allocate memory for hash table, trying with half\n");

    // try to allocate with half size
    init_hash_table(engine, mb / 2);
  }

  // if allocation succeeded
  else {
    // clear hash table
    clear_hash_table();
  }
}

// read hash entry data
int read_hash_entry(position_t *pos, int alpha, int *move,
                    int beta, int depth) {
  // create a TT instance pointer to particular hash entry storing
  // the scoring data for the current board position if available
  tt_entry_t *hash_entry =
      &tt.hash_entry[pos->hash_key % tt.num_of_entries];

  // make sure we're dealing with the exact position we need
  if (hash_entry->hash_key == pos->hash_key) {
    // make sure that we match the exact depth our search is now at
    if (hash_entry->depth >= depth) {
      // extract stored score from TT entry
      int score = hash_entry->score;

      // retrieve score independent from the actual path
      // from root node (position) to current node (position)
      if (score < -mate_score)
        score += pos->ply;
      if (score > mate_score)
        score -= pos->ply;

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
void write_hash_entry(position_t *pos, int score, int depth,
                      int move, int hash_flag) {
  // create a TT instance pointer to particular hash entry storing
  // the scoring data for the current board position if available
  tt_entry_t *hash_entry =
      &tt.hash_entry[pos->hash_key % tt.num_of_entries];

  if (!(hash_entry->hash_key == 0 ||
        (hash_entry->age < tt.current_age ||
         hash_entry->depth <= depth))) {
    return;
  }

  // store score independent from the actual path
  // from root node (position) to current node (position)
  if (score < -mate_score)
    score -= pos->ply;
  if (score > mate_score)
    score += pos->ply;

  // write hash entry data
  hash_entry->hash_key = pos->hash_key;
  hash_entry->score = score;
  hash_entry->flag = hash_flag;
  hash_entry->depth = depth;
  hash_entry->move = move;
  hash_entry->age = tt.current_age;
}
