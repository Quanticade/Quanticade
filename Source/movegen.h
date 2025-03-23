#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "structs.h"

void add_move(moves *move_list, int move);
uint8_t make_move(position_t* pos, uint16_t move);
void generate_moves(position_t* pos, moves *move_list);
void generate_captures(position_t* pos, moves *move_list);

#endif
