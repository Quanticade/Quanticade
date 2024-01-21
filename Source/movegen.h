#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "structs.h"
int make_move(engine_t *engine, int move, int move_flag);
void generate_moves(engine_t *engine, moves *move_list);
void generate_captures(engine_t *engine, moves *move_list);

#endif
