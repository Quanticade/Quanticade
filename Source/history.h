#ifndef HISTORY_H
#define HISTORY_H

#include "structs.h"

uint64_t generate_pawn_key(position_t *pos);

void update_pawn_corrhist(thread_t *thread, int16_t static_eval, int16_t score,
                          uint8_t depth, uint8_t tt_flag);
int16_t adjust_static_eval(thread_t *thread, position_t *pos,
                           int16_t static_eval);
void update_capture_history_moves(thread_t *thread, moves *capture_moves,
                                  int best_move, uint8_t depth);
int16_t get_conthist_score(thread_t *thread, searchstack_t *ss, int move);
void update_quiet_histories(thread_t *thread, searchstack_t *ss,
                            moves *quiet_moves, int best_move, uint8_t depth);
int16_t get_correction_value(thread_t *thread, position_t *pos);
#endif
