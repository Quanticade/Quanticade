#ifndef TRANSPOSITION_H
#define TRANSPOSITION_H

#include "structs.h"
#include <sched.h>

typedef struct tt {
  tt_entry_t *hash_entry;
  size_t num_of_entries;
} tt_t;

extern tt_t tt;

// transposition table hash flags
#define HASH_FLAG_NONE 0
#define HASH_FLAG_EXACT 1
#define HASH_FLAG_LOWER_BOUND 2
#define HASH_FLAG_UPPER_BOUND 3

void clear_hash_table(void);
void prefetch_hash_entry(uint64_t hash_key);
uint8_t can_use_score(int alpha, int beta, int tt_score, uint8_t flag);
int16_t score_from_tt(position_t *pos, int16_t score);
tt_entry_t* read_hash_entry(position_t *pos, uint8_t *tt_hit);
void write_hash_entry(tt_entry_t *tt_entry, position_t *pos, int16_t score, int16_t static_eval,
                      uint8_t depth, uint16_t move, uint8_t hash_flag,
                      uint8_t tt_pv);
void init_hash_table(uint64_t mb);
uint64_t generate_hash_key(position_t *pos);
int hash_full(void);
#endif
