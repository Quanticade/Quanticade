#ifndef HISTORY_H
#define HISTORY_H

#include "structs.h"

void update_quiet_history_moves(thread_t *thread,
                                              moves *quiet_moves, int best_move,
                                              uint8_t depth);
void update_capture_history_moves(thread_t *thread,
                                                moves *capture_moves,
                                                int best_move, uint8_t depth);
void update_continuation_history_moves(thread_t *thread,
                                                     searchstack_t *ss,
                                                     moves *quiet_moves,
                                                     int best_move,
                                                     uint8_t depth);

#endif
