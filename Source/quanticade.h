#ifndef QUANTICADE_H
#define QUANTICADE_H

#include "structs.h"
void init_hash_table(engine_t *engine, tt_t* hash_table, int mb);
void clear_hash_table(tt_t* hash_table);
void search_position(engine_t *engine, tt_t* hash_table, int depth);
int make_move(engine_t *engine, int move, int move_flag);
void reset_time_control(engine_t *engine);
void generate_moves(engine_t *engine, moves *move_list);
void parse_fen(engine_t *engine, char *fen);
#endif
