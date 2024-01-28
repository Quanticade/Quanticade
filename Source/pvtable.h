#ifndef PVTABLE_H
#define PVTABLE_H

#include "structs.h"

#define no_hash_entry 100000

// transposition table hash flags
#define hash_flag_exact 0
#define hash_flag_alpha 1
#define hash_flag_beta 2

void clear_hash_table(tt_t *hash_table);
int read_hash_entry(position_t *pos, tt_t *hash_table, int alpha, int *move,
                    int beta, int depth);
void write_hash_entry(position_t *pos, tt_t *hash_table, int score, int depth,
                      int move, int hash_flag);
void init_hash_table(engine_t *engine, tt_t *hash_table, uint64_t mb);
uint64_t generate_hash_key(engine_t *engine, position_t *pos);
#endif
