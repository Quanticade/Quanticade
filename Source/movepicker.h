#ifndef MOVEPICKER_H
#define MOVEPICKER_H

#include "move.h"
#include "structs.h"

typedef struct {
    moves *move_list;
    uint16_t index;
} move_picker_t;

// Initialize move picker with a move list
void init_picker(move_picker_t *picker, moves *move_list);

// Score all moves in the list
void score_moves(position_t *pos, thread_t *thread, searchstack_t *ss, 
                 moves *move_list, uint16_t tt_move);

// Get next best move from the list
// Returns 0 when no more moves
uint16_t next_move(move_picker_t *picker);

#endif