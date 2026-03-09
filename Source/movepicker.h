#ifndef MOVEPICKER_H
#define MOVEPICKER_H

#include "structs.h"

void init_picker(picker_t *picker, position_t *pos,
                                thread_t *thread, searchstack_t *ss,
                                uint16_t tt_move, uint8_t generate_all);
uint16_t select_next(picker_t *picker);

#endif