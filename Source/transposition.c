#include "transposition.h"
#include "bitboards.h"
#include "enums.h"
#include "structs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

tt_t tt;
extern keys_t keys;

__extension__ typedef unsigned __int128 uint128_t;

int hash_full(void) {
  uint64_t used = 0;
  int samples = 1000;

  for (int i = 0; i < samples; ++i)
    if (tt.hash_entry[i].move != 0)
      used++;

  return used / (samples / 1000);
}

static inline uint64_t get_hash_index(uint64_t hash) {
  return ((uint128_t)hash * (uint128_t)tt.num_of_entries) >> 64;
}

static inline uint32_t get_hash_low_bits(uint64_t hash) {
  return (uint32_t)hash;
}

uint64_t generate_hash_key(position_t *pos) {
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
      final_key ^= keys.piece_keys[piece][square];

      // pop LS1B
      pop_bit(bitboard, square);
    }
  }

  // if enpassant square is on board
  if (pos->enpassant != no_sq)
    // hash enpassant
    final_key ^= keys.enpassant_keys[pos->enpassant];

  // hash castling rights
  final_key ^= keys.castle_keys[pos->castle];

  // hash the side only if black is to move
  if (pos->side == black)
    final_key ^= keys.side_key;

  // return generated hash key
  return final_key;
}

void clear_hash_table(void) {
  memset(tt.hash_entry, 0, sizeof(tt_entry_t) * tt.num_of_entries);
}

// dynamically allocate memory for hash table
void init_hash_table(uint64_t mb) {
  // init hash size
  uint64_t hash_size = 0x100000LL * mb;

  // init number of hash entries
  tt.num_of_entries = hash_size / sizeof(tt_entry_t);

  // free hash table if not empty
  if (tt.mem != NULL) {
    printf("    Clearing hash memory...\n");

    // free hash table dynamic memory
    free(tt.mem);
  }

  // allocate memory
  tt.mem = malloc(tt.num_of_entries * sizeof(tt_t) + 64 - 1);

  // if allocation has failed
  if (tt.mem == NULL) {
    printf("    Couldn't allocate memory for hash table, trying with half\n");

    // try to allocate with half size
    init_hash_table(mb / 2);
  }

  // if allocation succeeded
  else {
    tt.hash_entry = (tt_entry_t *)(((uintptr_t)tt.mem + 64 - 1) & ~(64 - 1));
    // clear hash table
    clear_hash_table();
  }
}

// read hash entry data
int read_hash_entry(position_t *pos, int alpha, int beta,
                    int depth, int *move, uint16_t *tt_score) {
  (void)tt_score;
  // create a TT instance pointer to particular hash entry storing
  // the scoring data for the current board position if available
  tt_entry_t *hash_entry = &tt.hash_entry[get_hash_index(pos->hash_key)];

  // make sure we're dealing with the exact position we need
  if (hash_entry->hash_key == get_hash_low_bits(pos->hash_key)) {
    // make sure that we match the exact depth our search is now at
    *move = hash_entry->move;
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
        return score;

      // match beta (fail-high node) score
      if ((hash_entry->flag == hash_flag_beta) && (score >= beta))
        // return beta (fail-high node) score
        return score;
    }
  }

  // if hash entry doesn't exist
  return no_hash_entry;
}

// write hash entry data
void write_hash_entry(position_t *pos, int score, int depth, int move,
                      int hash_flag) {
  // create a TT instance pointer to particular hash entry storing
  // the scoring data for the current board position if available
  tt_entry_t *hash_entry = &tt.hash_entry[get_hash_index(pos->hash_key)];

  uint8_t replace = hash_entry->hash_key != get_hash_low_bits(pos->hash_key) ||
                    depth + 4 > hash_entry->depth ||
                    hash_flag == hash_flag_exact;

  if (!replace) {
    return;
  }

  // store score independent from the actual path
  // from root node (position) to current node (position)
  if (score < -mate_score)
    score -= pos->ply;
  if (score > mate_score)
    score += pos->ply;

  // write hash entry data
  hash_entry->hash_key = get_hash_low_bits(pos->hash_key);
  hash_entry->score = score;
  hash_entry->flag = hash_flag;
  hash_entry->depth = depth;
  hash_entry->move = move;
}
