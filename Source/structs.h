#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdint.h>

typedef struct tt {
  uint64_t hash_key; // "almost" unique chess position identifier
  int depth;         // current search depth
  int flag;          // flag the type of node (fail-low/fail-high/PV)
  int score;         // score (alpha/beta/PV)
  int move;
} tt;

// move list structure
typedef struct moves {
  int moves[256];
  uint32_t count;
} moves;

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
  board_t board;
  masks_t masks;
  attacks_t attacks;
  uint64_t repetition_table[1000];
  uint32_t repetition_index;
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
} engine_t;

#endif
