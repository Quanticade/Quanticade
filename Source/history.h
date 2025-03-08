#ifndef HISTORY_H
#define HISTORY_H

#include "structs.h"

void update_quiet_history_moves(thread_t *thread, moves *quiet_moves,
                                int best_move, uint8_t depth);
void update_capture_history_moves(thread_t *thread, moves *capture_moves,
                                  int best_move, uint8_t depth);
void update_continuation_history_moves(thread_t *thread, searchstack_t *ss,
                                       moves *quiet_moves, int bonus,
                                       int best_move, uint8_t depth);
void update_continuation_history(thread_t *thread, searchstack_t *ss, int move,
                                 int bonus, uint8_t depth,
                                 uint8_t is_best_move);
int16_t get_conthist_score(thread_t *thread, searchstack_t *ss, int move);

#endif
