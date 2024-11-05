#ifndef STRUCTS_H
#define STRUCTS_H

#include "bitboards.h"
#include <stdint.h>

#define MAX_PLY 254

typedef struct spsa {
  void* value;
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
} spsa_t;

typedef struct tt_entry {
  uint32_t hash_key; // "almost" unique chess position identifier
  int move;
  int16_t score; // score (alpha/beta/PV)
  uint8_t depth; // current search depth
  uint8_t flag;  // flag the type of node (fail-low/fail-high/PV)
} tt_entry_t;

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

typedef struct accumulator {
  _Alignas(64) int16_t accumulator[2][2048]; // This is very cursed but for now
                                             // lets have it this way
} accumulator_t;

typedef struct position {
  uint64_t bitboards[12];
  uint64_t occupancies[3];
  uint64_t hash_key;
  uint64_t repetition_table[1000];
  uint32_t repetition_index;
  uint32_t ply;
  uint32_t seldepth;
  uint32_t fifty;
  int32_t excluded_move;
  uint8_t mailbox[64];
  uint8_t side;
  uint8_t enpassant;
  uint8_t castle;
} position_t;

typedef struct PV {
  int32_t pv_length[MAX_PLY];
  int32_t pv_table[MAX_PLY][MAX_PLY];
  uint8_t follow_pv;
  uint8_t score_pv;
} PV_t;

typedef struct searchinfo {
  accumulator_t accumulator[MAX_PLY + 4];
  position_t pos;
  uint64_t nodes;
  uint64_t starttime;
  uint64_t stoptime;
  int score;
  int killer_moves[MAX_PLY];
  int16_t quiet_history[12][64][64];
  int16_t capture_history[2][64][64];
  PV_t pv;
  uint8_t depth;
  uint8_t timeset;
  uint8_t stopped;
  uint8_t quit;
  uint8_t index;
} thread_t;

typedef struct limits {
  int64_t time;
  int32_t inc;
  uint8_t depth;
  uint16_t movestogo;
} limits_t;

typedef struct searchthread {
  position_t *pos;
  thread_t *threads;
  char line[10000];
} searchthreadinfo_t;

typedef struct searchstack {
  int excluded_move;
  int static_eval;
} searchstack_t;

typedef struct nnue_settings {
  char *nnue_file;
} nnue_settings_t;

#endif
