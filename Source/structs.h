#ifndef STRUCTS_H
#define STRUCTS_H

#include "bitboards.h"
#include <stdint.h>

#define MAX_PLY 254

typedef struct spsa {
  void *value;
  union {
    uint64_t min_int;
    double min_float;
  } min;
  union {
    uint64_t max_int;
    double max_float;
  } max;
  double rate;
  void (*func)(void);
  char name[50];
  uint8_t is_float;
  uint8_t tunable;
} spsa_t;

typedef struct tt_entry {
  uint32_t hash_key; // "almost" unique chess position identifier
  uint16_t move;
  int16_t score; // score (alpha/beta/PV)
  int16_t static_eval;
  uint8_t depth;    // current search depth
  uint8_t flag : 2; // flag the type of node (fail-low/fail-high/PV)
  uint8_t tt_pv : 1;
} tt_entry_t;

typedef struct move {
  int score;
  uint16_t move;
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

typedef struct accumulator {
  _Alignas(64) int16_t accumulator[2][2048]; // This is very cursed but for now
                                             // lets have it this way
} accumulator_t;

typedef struct finny_table {
  accumulator_t accumulators;
  uint64_t bitboards[2][12];
} finny_table_t;

typedef struct hash_keys {
  uint64_t hash_key;
  uint64_t pawn_key;
} hash_keys_t;

typedef struct position {
  uint64_t bitboards[12];
  uint64_t occupancies[3];
  hash_keys_t hash_keys;
  uint8_t ply;
  uint8_t fifty;
  uint8_t mailbox[64];
  uint8_t side;
  uint8_t enpassant;
  uint8_t castle;
} position_t;

typedef struct PV {
  int32_t pv_length[MAX_PLY + 1];
  int32_t pv_table[MAX_PLY + 1][MAX_PLY + 1];
} PV_t;

typedef struct searchinfo {
  accumulator_t accumulator[MAX_PLY + 4];
  finny_table_t finny_tables[2][13];
  position_t pos;
  PV_t pv;
  uint64_t nodes;
  uint64_t starttime;
  uint64_t repetition_table[1000];
  uint32_t repetition_index;
  uint16_t killer_moves[MAX_PLY];
  uint16_t index;
  int16_t score;
  int16_t quiet_history[12][64][64];
  int16_t capture_history[12][13][64][64];
  int16_t continuation_history[12][64][12][64];
  int16_t correction_history[2][16384];
  uint8_t depth;
  uint8_t seldepth;
  uint8_t stopped;
  uint8_t quit;
} thread_t;

typedef struct limits {
  uint64_t soft_limit;
  uint64_t hard_limit;
  uint64_t node_limit;
  uint64_t start_time;
  int64_t time;
  int32_t inc;
  uint32_t base_soft;
  uint32_t max_time;
  uint16_t movestogo;
  uint8_t depth;
  uint8_t timeset;
  uint8_t nodes_set;
} limits_t;

typedef struct searchthread {
  position_t *pos;
  thread_t *threads;
  char line[10000];
} searchthreadinfo_t;

typedef struct searchstack {
  uint16_t move;
  int16_t static_eval;
  uint16_t excluded_move;
  int16_t reduction;
  int history_score;
  uint8_t piece;
  uint8_t null_move;
} searchstack_t;

typedef struct nnue_settings {
  char *nnue_file;
} nnue_settings_t;

#endif
