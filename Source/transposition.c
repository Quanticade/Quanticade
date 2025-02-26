#include "transposition.h"
#include "bitboards.h"
#include "enums.h"
#include "structs.h"
#include <stdint.h>
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
    if (tt.hash_entry[i].flag != HASH_FLAG_NONE)
      used++;

  return used / (samples / 1000);
}

static inline uint64_t get_hash_index(uint64_t hash) {
  return ((uint128_t)hash * (uint128_t)tt.num_of_entries) >> 64;
}

static inline uint32_t get_hash_low_bits(uint64_t hash) {
  return (uint32_t)hash;
}

void prefetch_hash_entry(uint64_t hash_key) {
  const uint64_t index = get_hash_index(hash_key);
  __builtin_prefetch(&tt.hash_entry[index]);
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
  if (tt.hash_entry != NULL) {
    printf("    Clearing hash memory...\n");

    // free hash table dynamic memory
    free(tt.hash_entry);
  }

  // allocate memory
  tt.hash_entry = malloc(tt.num_of_entries * sizeof(tt_entry_t));

  // if allocation has failed
  if (tt.hash_entry == NULL) {
    printf("    Couldn't allocate memory for hash table, trying with half\n");

    // try to allocate with half size
    init_hash_table(mb / 2);
  }

  // if allocation succeeded
  else {
    // clear hash table
    clear_hash_table();
  }
}

uint8_t can_use_score(int alpha, int beta, int tt_score, uint8_t flag) {
  if (tt_score != NO_SCORE &&
      ((flag == HASH_FLAG_EXACT) ||
       ((flag == HASH_FLAG_UPPER_BOUND) && (tt_score <= alpha)) ||
       ((flag == HASH_FLAG_LOWER_BOUND) && (tt_score >= beta)))) {
    return 1;
  }
  return 0;
}

// read hash entry data
uint8_t read_hash_entry(position_t *pos, tt_entry_t *tt_entry) {
  tt_entry_t *hash_entry = &tt.hash_entry[get_hash_index(pos->hash_keys.hash_key)];

  // make sure we're dealing with the exact position we need
  if (hash_entry->hash_key == get_hash_low_bits(pos->hash_keys.hash_key)) {
    int score = hash_entry->score;
    if (score < -MATE_SCORE)
      score += pos->ply;
    if (score > MATE_SCORE)
      score -= pos->ply;

    tt_entry->move = hash_entry->move;
    tt_entry->score = score;
    tt_entry->depth = hash_entry->depth;
    tt_entry->flag = hash_entry->flag;
    tt_entry->tt_pv = hash_entry->tt_pv;
    return 1;
  }

  // if hash entry doesn't exist
  return 0;
}

// write hash entry data
void write_hash_entry(position_t *pos, int16_t score, int16_t static_eval,
                      uint8_t depth, uint16_t move, uint8_t hash_flag,
                      uint8_t tt_pv) {
  // create a TT instance pointer to particular hash entry storing
  // the scoring data for the current board position if available
  tt_entry_t *hash_entry = &tt.hash_entry[get_hash_index(pos->hash_keys.hash_key)];

  uint8_t replace = hash_entry->hash_key != get_hash_low_bits(pos->hash_keys.hash_key) ||
                    depth + 4 > hash_entry->depth ||
                    hash_flag == HASH_FLAG_EXACT;

  if (!replace) {
    return;
  }

  // store score independent from the actual path
  // from root node (position) to current node (position)
  if (score < -MATE_SCORE)
    score -= pos->ply;
  if (score > MATE_SCORE)
    score += pos->ply;

  // write hash entry data
  hash_entry->hash_key = get_hash_low_bits(pos->hash_keys.hash_key);
  hash_entry->score = score;
  hash_entry->static_eval = static_eval;
  hash_entry->flag = hash_flag;
  hash_entry->tt_pv = tt_pv;
  hash_entry->depth = depth;
  hash_entry->move = move;
}
