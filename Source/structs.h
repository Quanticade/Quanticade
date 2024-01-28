#ifndef STRUCTS_H
#define STRUCTS_H

#include "bitboards.h"
#include <stdint.h>

typedef struct tt_entry {
  uint64_t hash_key; // "almost" unique chess position identifier
  int depth;         // current search depth
  int flag;          // flag the type of node (fail-low/fail-high/PV)
  int score;         // score (alpha/beta/PV)
  int move;
  uint16_t age;
} tt_entry_t;

typedef struct tt {
  tt_entry_t *hash_entry;
  uint32_t num_of_entries;
  uint16_t current_age;
} tt_t;

typedef struct move {
  int move;
  int score;
} move_t;

// move list structure
typedef struct moves {
  move_t entry[280];
  uint32_t count;
} moves;

typedef struct keys {
  uint64_t piece_keys[12][64];
  uint64_t enpassant_keys[64];
  uint64_t castle_keys[16];
  uint64_t side_key;
} keys_t;

typedef struct position {
  uint64_t bitboards[12];
  uint64_t occupancies[3];
  uint8_t side;
  uint8_t enpassant;
  uint8_t castle;
  uint64_t hash_key;
  uint64_t repetition_table[1000];
  uint32_t repetition_index;
  uint32_t ply;
  uint32_t fifty;
  int killer_moves[2][max_ply];
  int history_moves[12][64];
  int pv_length[max_ply];
  int pv_table[max_ply][max_ply];
  int follow_pv;
  int score_pv;
} position_t;

typedef struct searchinfo {
  uint8_t quit;
  uint16_t movestogo;
  int64_t time;
  int32_t inc;
  uint64_t starttime;
  uint64_t stoptime;
  uint8_t timeset;
  uint8_t stopped;
  uint64_t nodes;
} searchinfo_t;

typedef struct engine {
  keys_t keys;
  uint32_t random_state;
  uint8_t nnue;
  char *nnue_file;
} engine_t;

#endif
