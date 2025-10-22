#include "transposition.h"
#include "bitboards.h"
#include "enums.h"
#include "structs.h"
#include "uci.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

tt_t tt;
extern keys_t keys;

extern int thread_count;

__extension__ typedef unsigned __int128 uint128_t;

int hash_full(void) {
  uint64_t used = 0;
  int samples = 1000;

  for (int i = 0; i < samples; ++i) {
    for (int j = 0; j < 3; j++) {
      if (tt.hash_entry[i].tt_entries[j].hash_key != 0) {
        used++;
      }
    }
  }

  return used / ((samples * 3) / 1000);
}

static inline uint64_t get_hash_index(uint64_t hash) {
  return ((uint128_t)hash * (uint128_t)tt.num_of_entries) >> 64;
}

static inline uint16_t get_hash_low_bits(uint64_t hash) {
  return (uint16_t)hash;
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

typedef struct {
  size_t start;
  size_t end;
} thread_data_t;

void *clear_hash_chunk(void *arg) {
  thread_data_t *data = (thread_data_t *)arg;
  size_t count = data->end - data->start;
  memset(&tt.hash_entry[data->start], 0, sizeof(tt_bucket_t) * count);
  return NULL;
}

void clear_hash_table(void) {
  pthread_t threads[thread_count];
  thread_data_t thread_data[thread_count];

  size_t chunk_size =
      (tt.num_of_entries + thread_count - 1) / thread_count; // Ceiling division

  for (int i = 0; i < thread_count; i++) {
    size_t start = i * chunk_size;
    size_t end = MIN(start + chunk_size, tt.num_of_entries);

    thread_data[i].start = start;
    thread_data[i].end = end;

    pthread_create(&threads[i], NULL, clear_hash_chunk, &thread_data[i]);
  }

  for (int i = 0; i < thread_count; i++) {
    pthread_join(threads[i], NULL);
  }
}

// dynamically allocate memory for hash table
void init_hash_table(uint64_t mb) {
  // init hash size
  uint64_t hash_size = 0x100000LL * mb;

  // init number of hash entries
  tt.num_of_entries = hash_size / sizeof(tt_bucket_t);

  // free hash table if not empty
  if (tt.hash_entry != NULL) {
    // free hash table dynamic memory
    free(tt.hash_entry);
  }

  // allocate memory
  tt.hash_entry = malloc(tt.num_of_entries * sizeof(tt_bucket_t));

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

int16_t score_from_tt(position_t *pos, int16_t score) {

  if (score < -MATE_SCORE)
    score += pos->ply;
  if (score > MATE_SCORE)
    score -= pos->ply;
  return score;
}

// read hash entry data
tt_entry_t *read_hash_entry(position_t *pos, uint8_t *tt_hit) {
  tt_bucket_t *bucket = &tt.hash_entry[get_hash_index(pos->hash_keys.hash_key)];
  tt_entry_t *replace = &bucket->tt_entries[0];
  uint8_t min_depth = 255;

  for (uint8_t i = 0; i < 3; i++) {
    tt_entry_t *entry = &bucket->tt_entries[i];

    if (entry->hash_key == get_hash_low_bits(pos->hash_keys.hash_key)) {
      *tt_hit = 1;
      return entry;
    }

    if (entry->depth < min_depth) {
      replace = entry;
      min_depth = entry->depth;
    }
  }

  // if hash entry doesn't exist
  return replace;
}

// write hash entry data
void write_hash_entry(tt_entry_t *tt_entry, position_t *pos, int16_t score,
                      int16_t static_eval, uint8_t depth, uint16_t move,
                      uint8_t hash_flag, uint8_t tt_pv) {
  uint16_t key = get_hash_low_bits(pos->hash_keys.hash_key);
  uint8_t replace =
      tt_entry->hash_key != key ||
      depth + 4 > tt_entry->depth || hash_flag == HASH_FLAG_EXACT;

  if (!(tt_entry->hash_key == key && move == 0)) {
    tt_entry->move = move;
  }

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
  tt_entry->hash_key = key;
  tt_entry->score = score;
  tt_entry->static_eval = static_eval;
  tt_entry->flag = hash_flag;
  tt_entry->tt_pv = tt_pv;
  tt_entry->depth = depth;
}
