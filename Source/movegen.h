#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "structs.h"

uint8_t is_pseudo_legal(position_t *pos, uint16_t move);
void add_move(moves *move_list, int move);
uint8_t make_move(position_t* pos, uint16_t move);
void generate_moves(position_t* pos, moves *move_list);
void generate_noisy(position_t* pos, moves *move_list);

#endif
