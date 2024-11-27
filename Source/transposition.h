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
#define HASH_FLAG_ALPHA 2
#define HASH_FLAG_BETA 3

void clear_hash_table(void);
void prefetch_hash_entry(uint64_t hash_key);
int read_hash_entry(position_t *pos, int *move, int16_t *tt_score,
                    int16_t *tt_static_eval, uint8_t *tt_depth, uint8_t *tt_flag);
void write_hash_entry(position_t *pos, int16_t score, int16_t static_eval, uint8_t depth, int move,
                      int hash_flag);
void init_hash_table(uint64_t mb);
uint64_t generate_hash_key(position_t *pos);
int hash_full(void);
#endif
