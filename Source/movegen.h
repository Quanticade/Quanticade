#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "structs.h"

uint8_t is_pseudo_legal(position_t *pos, uint16_t move);
uint8_t is_legal(position_t *pos, uint16_t move);
void add_move(moves *move_list, int move);
void make_move(position_t* pos, uint16_t move);
void generate_moves(position_t* pos, moves *move_list);
void generate_noisy(position_t* pos, moves *move_list);
void init_between_bitboards(void);
void update_slider_pins(position_t *pos, uint8_t side);

#endif
