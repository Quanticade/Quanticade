#ifndef PVTABLE_H
#define PVTABLE_H

#include "structs.h"
void clear_hash_table(tt_t *hash_table);
int read_hash_entry(engine_t *engine, tt_t *hash_table, int alpha, int *move,
                    int beta, int depth);
void write_hash_entry(engine_t *engine, tt_t *hash_table, int score, int depth,
                      int move, int hash_flag);
void init_hash_table(engine_t *engine, tt_t *hash_table, int mb);
uint64_t generate_hash_key(engine_t *engine);
#endif
