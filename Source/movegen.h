#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "structs.h"

void add_move(moves *move_list, int move);
int make_move(position_t* pos, int move);
void generate_moves(position_t* pos, moves *move_list);
void generate_captures(position_t* pos, moves *move_list);

#endif
