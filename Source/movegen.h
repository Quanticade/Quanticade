#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "structs.h"
int make_move(engine_t *engine, board_t* board, int move, int move_flag);
void generate_moves(board_t* board, moves *move_list);
void generate_captures(board_t* board, moves *move_list);

#endif
