#ifndef STRUCTS_H
#define STRUCTS_H

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
  tt_entry_t* hash_entry;
  uint32_t num_of_entries;
  uint16_t current_age;
} tt_t;

// move list structure
typedef struct moves {
  int moves[256];
  uint32_t count;
} moves;

typedef struct keys {
  uint64_t piece_keys[12][64];
  uint64_t enpassant_keys[64];
  uint64_t castle_keys[16];
  uint64_t side_key;
} keys_t;

typedef struct attacks {
  uint64_t pawn_attacks[2][64];
  uint64_t knight_attacks[64];
  uint64_t king_attacks[64];
  uint64_t bishop_attacks[64][512];
  uint64_t rook_attacks[64][4096];
} attacks_t;

typedef struct masks {
  uint64_t bishop_masks[64];
  uint64_t rook_masks[64];
  uint64_t file_masks[64];
  uint64_t rank_masks[64];
  uint64_t isolated_masks[64];
  uint64_t white_passed_masks[64];
  uint64_t black_passed_masks[64];
} masks_t;

typedef struct board {
  uint64_t bitboards[12];
  uint64_t occupancies[3];
  uint8_t side;
  uint8_t enpassant;
  uint8_t castle;
  uint64_t hash_key;
} board_t;

typedef struct engine {
  attacks_t attacks;
  board_t board;
  keys_t keys;
  masks_t masks;
  uint64_t repetition_table[1000];
  uint32_t repetition_index;
  uint32_t random_state;
  uint32_t ply;
  uint8_t quit;
  uint16_t movestogo;
  int64_t time;
  int32_t inc;
  uint64_t starttime;
  uint64_t stoptime;
  uint8_t timeset;
  uint8_t stopped;
  uint8_t nnue;
  uint32_t fifty;
  uint64_t nodes;
  int killer_moves[2][64];
  int history_moves[12][64];
  int pv_length[64];
  int pv_table[64][64];
  int follow_pv;
  int score_pv;
} engine_t;

#endif
