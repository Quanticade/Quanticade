#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "structs.h"
int make_move(engine_t *engine, position_t* pos, int move, int move_flag);
void generate_moves(position_t* pos, moves *move_list);
void generate_captures(position_t* pos, moves *move_list);

#endif
